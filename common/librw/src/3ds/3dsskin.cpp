#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../rwbase.h"
#include "../rwerror.h"
#include "../rwplg.h"
#include "../rwrender.h"
#include "../rwengine.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwanim.h"
#include "../rwplugins.h"

#include "rw3ds.h"
#include "rw3dsplg.h"
#include "rw3dsimpl.h"
#include "rw3dsshader.h"
#include "default_shbin.h"

namespace rw {
namespace c3d {

#ifdef RW_3DS

  /* GPU gives us 92 float vectors. Game wants 256 vectors for bones...
     might as well do it on the cpu.
     currently bugged because skinning transform isn't applied per instance
     rather globally, the bugs are pretty funny to watch though... and it saves RAM.
     Or go back to rendering with immediate mode and flood the gx command buffer?
     Or do we just split the frame after rendering?
   */

static Matrix boneMatrices[64];
static Shader *skinTextureShader;

/* Geometry is shared by every instance of a ped model, but 3DS skinning is
 * performed on the CPU.  Writing the transformed vertices back into the
 * geometry's single buffer lets a later ped overwrite data which the GPU has
 * not consumed yet.  Keep one reusable buffer per skinned draw in the current
 * frame; beginUpdate resets the cursor only after C3D_FRAME_SYNCDRAW. */
enum {
	NUM_SKIN_FRAME_BUFFERS = 192,
	SKIN_FRAME_BUFFER_BUDGET = 3 << 20
};
struct SkinFrameBuffer {
	uint8 *data;
	size_t size;
};
static SkinFrameBuffer skinFrameBuffers[NUM_SKIN_FRAME_BUFFERS];
static int32 numSkinFrameBuffersUsed;
static size_t skinFrameBufferBytes;

#ifdef RESTORIES_3DS_BUILD
/* LCS cutscene actors use 53-bone meshes of up to about 60 KiB each.  They are
 * streamed at the point where linear memory is tightest, so the general pool's
 * 4 MiB safety margin can otherwise send them back to the shared geometry
 * buffer.  Reserve enough non-shared storage for the maximum three CG actors
 * used by an LCS cutscene, plus one spare, while memory is still plentiful. */
enum {
	NUM_LCS_CG_SKIN_BUFFERS = 4,
	LCS_CG_SKIN_BUFFER_SIZE = 64 << 10
};
static uint8 *lcsCgSkinBuffers[NUM_LCS_CG_SKIN_BUFFERS];
static int32 numLcsCgSkinBuffersUsed;
#endif

void
resetSkinFrameBuffers(void)
{
	numSkinFrameBuffersUsed = 0;
#ifdef RESTORIES_3DS_BUILD
	numLcsCgSkinBuffersUsed = 0;
#endif
}

#ifdef RESTORIES_3DS_BUILD
static uint8*
acquireLcsCgSkinBuffer(size_t size)
{
	if(size > LCS_CG_SKIN_BUFFER_SIZE ||
	   numLcsCgSkinBuffersUsed >= NUM_LCS_CG_SKIN_BUFFERS)
		return nil;
	return lcsCgSkinBuffers[numLcsCgSkinBuffersUsed++];
}
#endif

static uint8*
acquireSkinFrameBuffer(size_t size)
{
	if(numSkinFrameBuffersUsed >= NUM_SKIN_FRAME_BUFFERS)
		return nil;

	SkinFrameBuffer &slot = skinFrameBuffers[numSkinFrameBuffersUsed++];
	if(slot.size < size){
		size_t grownTotal = skinFrameBufferBytes - slot.size + size;
		/* Stay well clear of the texture/streaming red line.  If a pathological
		 * scene exceeds the bounded scratch budget, degrade only the overflow
		 * draws to the old shared buffer instead of consuming all linear memory. */
		if(grownTotal > SKIN_FRAME_BUFFER_BUDGET || linearSpaceFree() < size + (4 << 20))
			return nil;
		uint8 *newData = (uint8*)linearAlloc(size);
		if(newData == nil)
			return nil;
		if(slot.data)
			linearFree(slot.data);
		slot.data = newData;
		skinFrameBufferBytes = grownTotal;
		slot.size = size;
	}
	return slot.data;
}

static inline uint32
packTevMaterialColor(const RGBA &color)
{
	return ((uint32)color.alpha << 24) | ((uint32)color.blue << 16) |
	       ((uint32)color.green << 8) | color.red;
}

static void
combinerSkinTextureOnly(void)
{
	C3D_TexEnv *env0 = C3D_GetTexEnv(0);
	C3D_TexEnv *env1 = C3D_GetTexEnv(1);
	C3D_TexEnvInit(env0);
	C3D_TexEnvInit(env1);
	/* Keep skin RGB independent of the broken per-vertex lighting path, but
	 * preserve texture and material/vertex alpha for translucent ped meshes. */
	C3D_TexEnvSrc(env0, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT);
	C3D_TexEnvFunc(env0, C3D_RGB, GPU_MODULATE);
	C3D_TexEnvSrc(env0, C3D_Alpha, GPU_TEXTURE0, GPU_PRIMARY_COLOR);
	C3D_TexEnvFunc(env0, C3D_Alpha, GPU_MODULATE);
}

void
skinInstanceCB(Geometry *geo, InstanceDataHeader *header, bool32 reinstance)
{
	AttribDesc *attribs = header->attribDesc;
	bool isPrelit = !!(geo->flags & Geometry::PRELIT);
	if(!reinstance){
		/* Create attribute descriptions and nothing else */
		memset(attribs, 0, sizeof(header->attribDesc));
		
		header->numAttribs = 0;
		header->stride = 0;

#define ATTRIB(_REG, _COUNT, _GPU_TYPE, _NAT_TYPE)			\
		{							\
 			attribs[_REG].index  = header->numAttribs;	\
 			attribs[_REG].offset = header->stride;		\
			attribs[_REG].count  = _COUNT;			\
 			attribs[_REG].type   = _GPU_TYPE;		\
 			header->stride += sizeof(_NAT_TYPE) * _COUNT;	\
 			header->numAttribs++;				\
 		}

		// Positions
		ATTRIB(ATTRIB_POS, 3, GPU_FLOAT, float32);
		
		/* skinTextureShader deliberately ignores lighting RGB to avoid the 3DS
		 * black-diamond bug.  Do not reserve/flush four unused normal bytes per
		 * vertex or CPU-skin those normals every draw. */
		
		// Prelighting
		if(isPrelit){
			ATTRIB(ATTRIB_COLOR, 4, GPU_UNSIGNED_BYTE, uint8);
		}
		
		// Texture coordinates
		if(geo->numTexCoordSets){
			ATTRIB(ATTRIB_TEXCOORDS0, 2, GPU_FLOAT, float32);
		}

		header->vertexBuffer = (uint8*)safeLinearAlloc(header->totalNumVertex * header->stride);
	}

	genAttribPointers(header);
}

void
skinUninstanceCB(Geometry *geo, InstanceDataHeader *header)
{
	assert(0 && "can't uninstance");
}

static void
immSkinPos(uint8 *out, V3d *in_pos, float *in_weights, uint8 *in_indices)
{
	V3d v_pos = { 0.0f, 0.0f, 0.0f };

	/* Avoid four out-of-line transformPoints calls for every vertex.  Most VC
	 * skin vertices have only one or two non-zero influences, so skip empty
	 * slots and accumulate the affine transform directly. */
	for (int i = 0; i < 4; i++){
		const float weight = in_weights[i];
		if(weight == 0.0f)
			continue;
		const Matrix &m = boneMatrices[in_indices[i]];
		v_pos.x += weight * (in_pos->x*m.right.x + in_pos->y*m.up.x + in_pos->z*m.at.x + m.pos.x);
		v_pos.y += weight * (in_pos->x*m.right.y + in_pos->y*m.up.y + in_pos->z*m.at.y + m.pos.y);
		v_pos.z += weight * (in_pos->x*m.right.z + in_pos->y*m.up.z + in_pos->z*m.at.z + m.pos.z);
	}

	memcpy(out, &v_pos, 12);
}

static void
transformGeometry(Atomic *atomic, InstanceDataHeader *header, uint8 *dst)
{
	Geometry     *geo = atomic->geometry;
	Skin        *skin = Skin::get(geo);
	AttribDesc  *attr = header->attribDesc;
	float  *b_weights = skin->weights;
	uint8  *b_indices = skin->indices;
	V3d       *in_pos = geo->morphTargets[0].vertices;
	RGBA      *in_col = geo->colors;
	TexCoords *in_tex = geo->texCoords[0];
	int         i, nv = header->totalNumVertex;

	for(i = 0; i < nv; i++){
		immSkinPos(dst, &in_pos[i], &b_weights[i * 4], &b_indices[i * 4]);
		dst += 12;
		
		if(attr[ATTRIB_COLOR].count){
			memcpy(dst, &in_col[i], 4);
			dst += 4;
		}
		
		if(attr[ATTRIB_TEXCOORDS0].count){
			memcpy(dst, &in_tex[i], 8);
			dst += 8;
		}
	}
}

static void
uploadSkinMatrices(Atomic *atomic)
{
	int i;
	Skin *skin = Skin::get(atomic->geometry);
	Matrix *m = (Matrix*)boneMatrices;
	HAnimHierarchy *hier = Skin::getHierarchy(atomic);

	if(hier){
		Matrix *invMats = (Matrix*)skin->inverseMatrices;
		Matrix tmp;

		assert(skin->numBones == hier->numNodes);
		if(hier->flags & HAnimHierarchy::LOCALSPACEMATRICES){
			for(i = 0; i < hier->numNodes; i++){
				invMats[i].flags = 0;
				Matrix::mult(m, &invMats[i], &hier->matrices[i]);
				m++;
			}
		}else{
			Matrix invAtmMat;
			Matrix::invert(&invAtmMat, atomic->getFrame()->getLTM());
			for(i = 0; i < hier->numNodes; i++){
				invMats[i].flags = 0;
				Matrix::mult(&tmp, &hier->matrices[i], &invAtmMat);
				Matrix::mult(m, &invMats[i], &tmp);
				m++;
			}
		}
	}else{
		for(i = 0; i < skin->numBones; i++){
			m->setIdentity();
			m++;
		}
	}
}
  
void
skinRenderCB(Atomic *atomic, InstanceDataHeader *header)
{
	uint32       flags = atomic->geometry->flags;
	InstanceData *inst = header->inst;
	int32            n = header->numMeshes;
	Material      *mat;

	size_t bufferSize = header->totalNumVertex * header->stride;
	uint8 *sharedBuffer = header->vertexBuffer;
#ifdef RE3_3DS_BUILD
	/* GTA III's ordinary peds are rigid limb atomics rather than skinned
	 * geometry.  Keep the original low-memory path for the few skin atomics
	 * which can occur in re3; the visible ped fix belongs in PedModelInfo. */
	uint8 *frameBuffer = sharedBuffer;
#else
	uint8 *frameBuffer = nil;
#ifdef RESTORIES_3DS_BUILD
	Skin *skin = Skin::get(atomic->geometry);
	if(skin->numBones > 24)
		frameBuffer = acquireLcsCgSkinBuffer(bufferSize);
#endif
	if(frameBuffer == nil)
		frameBuffer = acquireSkinFrameBuffer(bufferSize);
	if(frameBuffer == nil)
		frameBuffer = sharedBuffer;
#endif

	uploadSkinMatrices(atomic);
	transformGeometry(atomic, header, frameBuffer);
	GSPGPU_FlushDataCache(frameBuffer, bufferSize);
	header->vertexBuffer = frameBuffer;
	genAttribPointers(header);
	
	setWorldMatrix(atomic->getFrame()->getLTM());
	setAttribPointers(header);
	/* The lightweight shader calculates only position, UV and the alpha used by
	 * this texture/material TEV path.  It has no normal input, light loop or
	 * unused RGB output calculation. */
	skinTextureShader->use();

	while(n--){
		mat = inst->material;
		setMaterialColor(flags, mat->color);
		C3D_TexEnvColor(C3D_GetTexEnv(0), packTevMaterialColor(mat->color));
		setTexture(0, mat->texture);

		rw::SetRenderState(VERTEXALPHA, inst->vertexAlpha || mat->color.alpha != 0xFF);
		drawInst_simple(header, inst);
		inst++;
	}

	/* Preserve the geometry-owned pointer for destruction and for the unlikely
	 * overflow fallback.  C3D_SetBufInfo has already copied this draw's address
	 * into the command buffer. */
	header->vertexBuffer = sharedBuffer;
	genAttribPointers(header);
#ifdef RE3_3DS_BUILD
	C3D_FrameSplit(0);
#endif
}
  
static void*
skinOpen(void *o, int32, int32)
{
	skinGlobals.pipelines[PLATFORM_3DS] = makeSkinPipeline();
	skinTextureShader = Shader::create(VSH_PRG_MATFXBASE, combinerSkinTextureOnly, false);
	assert(skinTextureShader);
#ifdef RESTORIES_3DS_BUILD
	for(int32 i = 0; i < NUM_LCS_CG_SKIN_BUFFERS; i++){
		lcsCgSkinBuffers[i] = (uint8*)linearAlloc(LCS_CG_SKIN_BUFFER_SIZE);
		assert(lcsCgSkinBuffers[i]);
	}
#endif
	return o;
}

static void*
skinClose(void *o, int32, int32)
{
	((ObjPipeline*)skinGlobals.pipelines[PLATFORM_3DS])->destroy();
	skinGlobals.pipelines[PLATFORM_3DS] = nil;
	skinTextureShader->destroy();
	skinTextureShader = nil;
	for(int32 i = 0; i < NUM_SKIN_FRAME_BUFFERS; i++){
		if(skinFrameBuffers[i].data)
			linearFree(skinFrameBuffers[i].data);
		skinFrameBuffers[i].data = nil;
		skinFrameBuffers[i].size = 0;
	}
	numSkinFrameBuffersUsed = 0;
	skinFrameBufferBytes = 0;
#ifdef RESTORIES_3DS_BUILD
	for(int32 i = 0; i < NUM_LCS_CG_SKIN_BUFFERS; i++){
		if(lcsCgSkinBuffers[i])
			linearFree(lcsCgSkinBuffers[i]);
		lcsCgSkinBuffers[i] = nil;
	}
	numLcsCgSkinBuffersUsed = 0;
#endif
	return o;
}

void
initSkin(void)
{
	Driver::registerPlugin(PLATFORM_3DS, 0, ID_SKIN,
	                       skinOpen, skinClose);
}

ObjPipeline*
makeSkinPipeline(void)
{
	ObjPipeline  *pipe = ObjPipeline::create();
	pipe->instanceCB   = skinInstanceCB;
	pipe->uninstanceCB = skinUninstanceCB;
	pipe->renderCB     = skinRenderCB;
	pipe->pluginID     = ID_SKIN;
	pipe->pluginData   = 1;
	return pipe;
}
  
#else

void initSkin(void) { }
void resetSkinFrameBuffers(void) { }

#endif

}
}
