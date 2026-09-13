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
	
void
skinInstanceCB(Geometry *geo, InstanceDataHeader *header, bool32 reinstance)
{
	AttribDesc *attribs = header->attribDesc;
	bool isPrelit = !!(geo->flags & Geometry::PRELIT);
	bool hasNormals = !!(geo->flags & Geometry::NORMALS);
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
		
		// Normals
		if(hasNormals){
			ATTRIB(ATTRIB_NORMAL, 4, GPU_BYTE, int8);
		}
		
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
	V3d tmp;
	int i;
	
	for (i = 0; i < 4; i++){
		V3d::transformPoints(&tmp, in_pos, 1, &boneMatrices[in_indices[i]]);
		v_pos = add(v_pos, scale(tmp, in_weights[i]));
	}

	memcpy(out, &v_pos, 12);
}

static void
immSkinNrm(int8 *out, V3d *in_nrm, float *in_weights, uint8 *in_indices)
{
	V3d v_nrm = { 0.0f, 0.0f, 0.0f };
	V3d tmp;
	int i;
	
	for (i = 0; i < 4; i++){
		V3d::transformVectors(&tmp, in_nrm, 1, &boneMatrices[in_indices[i]]);
		v_nrm = add(v_nrm, scale(tmp, in_weights[i]));
	}

	/* use the largest vector we can fit in the byte space */
	float m = fmax(fabs(v_nrm.x), fmax(fabs(v_nrm.y), fabs(v_nrm.z)));
	float s = 127.0f / m;
	out[0] = (int8)(s * v_nrm.x);
	out[1] = (int8)(s * v_nrm.y);
	out[2] = (int8)(s * v_nrm.z);
	out[3] = 0;
}
	
static void
transformGeometry(Atomic *atomic, InstanceDataHeader *header)
{
	Geometry     *geo = atomic->geometry;
	Skin        *skin = Skin::get(geo);
	AttribDesc  *attr = header->attribDesc;
	float  *b_weights = skin->weights;
	uint8  *b_indices = skin->indices;
	V3d       *in_pos = geo->morphTargets[0].vertices;
	V3d       *in_nrm = geo->morphTargets[0].normals;
	RGBA      *in_col = geo->colors;
	TexCoords *in_tex = geo->texCoords[0];
	uint8        *dst = header->vertexBuffer;
	int         i, nv = header->totalNumVertex;

	for(i = 0; i < nv; i++){
		immSkinPos(dst, &in_pos[i], &b_weights[i * 4], &b_indices[i * 4]);
		dst += 12;
		
		if(attr[ATTRIB_NORMAL].count){
			immSkinNrm(dst, &in_nrm[i], &b_weights[i * 4], &b_indices[i * 4]);
			dst += 4;
		}
		
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

	uploadSkinMatrices(atomic);
	transformGeometry(atomic, header);
	
	setWorldMatrix(atomic->getFrame()->getLTM());
	lightingCB(atomic);
	
	setAttribPointers(header);
	defaultShader->use();

	while(n--){
		mat = inst->material;
		setMaterial(flags, mat->color, mat->surfaceProps);
		setTexture(0, mat->texture);

		rw::SetRenderState(VERTEXALPHA, inst->vertexAlpha || mat->color.alpha != 0xFF);
		drawInst_simple(header, inst);
		inst++;
	}

	/* this doesn't seem to work, still get people "driving" down the sidewalk */
	size_t  size = header->totalNumVertex * header->stride;
	GSPGPU_FlushDataCache(header->vertexBuffer, size);
	C3D_FrameSplit(0);
}
  
static void*
skinOpen(void *o, int32, int32)
{
	skinGlobals.pipelines[PLATFORM_3DS] = makeSkinPipeline();
	return o;
}

static void*
skinClose(void *o, int32, int32)
{
	((ObjPipeline*)skinGlobals.pipelines[PLATFORM_3DS])->destroy();
	skinGlobals.pipelines[PLATFORM_3DS] = nil;
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

#endif

}
}

