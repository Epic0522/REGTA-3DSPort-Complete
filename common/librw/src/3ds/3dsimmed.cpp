#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../rwbase.h"
#include "../rwerror.h"
#include "../rwplg.h"
#include "../rwrender.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwengine.h"

#ifdef RW_3DS

#include "rw3ds.h"
#include "rw3dsimpl.h"

#include "rw3dsshader.h"
#include "default_shbin.h"

#include <tex3ds.h>

#define U(x) (VSH_FVEC_##x)

namespace rw {
namespace c3d {

#define TMPINDEX_NUM 1024
static u16 tmpIndex[TMPINDEX_NUM];
static Im2DVertex tmpprimbuf[3];
	
static C3D_AttrInfo im2DVao;
static C3D_AttrInfo im3DVao;
#ifdef RW_3DS_BUFFERED_IM3D
enum {
	IM3D_FRAME_BUFFER_COUNT = 12,
	IM3D_FRAME_MAX_VERTICES = 512,
	IM3D_FRAME_MAX_INDICES = 1024,
};
struct BufferedIm3DVertex
{
	float32 x, y, z, w;
	float32 r, g, b, a;
	float32 u, v;
};
static uint8 *im3DFrameBuffers[IM3D_FRAME_BUFFER_COUNT];
static int32 im3DFrameBufferCursor;
static bool32 im3DBuffered;

static uint8*
acquireIm3DFrameBuffer(int32 slot)
{
	if(im3DFrameBuffers[slot] == nil){
		const size_t size = IM3D_FRAME_MAX_VERTICES * sizeof(BufferedIm3DVertex) +
		                    IM3D_FRAME_MAX_INDICES * sizeof(u16);
		/* This path used to reserve the complete ring during renderer startup.
		 * VC is already close to its linear-memory high-water mark there, so that
		 * otherwise harmless water fix could panic before reaching the menu.
		 * Grow the ring only when water actually submits a batch and retain a
		 * safety margin; immediate mode remains a valid low-memory fallback. */
		if(linearSpaceFree() < size + (2 << 20))
			return nil;
		im3DFrameBuffers[slot] = (uint8*)linearAlloc(size);
	}
	return im3DFrameBuffers[slot];
}
#endif
	
static Shader *im2dShader;
static Shader *im3dShader;
Shader        *im2dOverrideShader;
  
static int32 im3D_vtx_count;
static Im3DVertex *im3D_vtx;
  
static int32
primTypeMap[] = {
	0,			// PRIMTYPENONE
	0,			// PRIMTYPELINELIST
	0,			// PRIMTYPEPOLYLIST
	GPU_TRIANGLES,		// PRIMTYPETRYLIST
	GPU_TRIANGLE_STRIP,	// PRIMTYPETRISTRIP
	GPU_TRIANGLE_FAN,	// PRIMTYPETRIFAN
	0			// PRIMTYPEPOINTLIST
};

void
openIm2D(void)
{
	im2dShader = Shader::create(VSH_PRG_IM2D, combiner_simple, false);
	assert(im2dShader);
	
	AttrInfo_Init(&im2DVao);
	AttrInfo_AddLoader(&im2DVao, 0, GPU_FLOAT, 4); // in_pos / v0
	AttrInfo_AddLoader(&im2DVao, 1, GPU_FLOAT, 4); // in_col / i0
	AttrInfo_AddLoader(&im2DVao, 2, GPU_FLOAT, 2); // in_tc0 / v2
}

void
closeIm2D(void)
{
	im2dShader->destroy();
	im2dShader = nil;
}

static void
im2DRenderPoint(void *vertices, int32 numVertices, int32 vert1)
{
	Im2DVertex *verts = (Im2DVertex*)vertices;
	tmpprimbuf[0] = verts[vert1];
	tmpprimbuf[1] = verts[vert1];
	tmpprimbuf[2] = verts[vert1];
	im2DRenderPrimitive(PRIMTYPETRILIST, tmpprimbuf, 3);
}
	
void
im2DRenderLine(void *vertices, int32 numVertices, int32 vert1, int32 vert2)
{
	Im2DVertex *verts = (Im2DVertex*)vertices;
	tmpprimbuf[0] = verts[vert1];
	tmpprimbuf[1] = verts[vert2];
	tmpprimbuf[2] = verts[vert1];
	im2DRenderPrimitive(PRIMTYPETRILIST, tmpprimbuf, 3);
}

void
im2DRenderTriangle(void *vertices, int32 numVertices, int32 vert1, int32 vert2, int32 vert3)
{
	Im2DVertex *verts = (Im2DVertex*)vertices;
	tmpprimbuf[0] = verts[vert1];
	tmpprimbuf[1] = verts[vert2];
	tmpprimbuf[2] = verts[vert3];
	im2DRenderPrimitive(PRIMTYPETRILIST, tmpprimbuf, 3);
}

void
im2DSetXform(void)
{
	Camera *cam = (Camera*)engine->currentCamera;
	int tilt = cameraTilt(cam);
	float w = cam->frameBuffer->width;
	float h = cam->frameBuffer->height;

	C3D_FVUnifSet(GPU_VERTEX_SHADER, U(u_xform), 2.0f/w, -2.0/h, -1, 1);
	
	if(!tilt){
		C3D_FVUnifSet(GPU_VERTEX_SHADER, U(u_flip), 1.0f, 0.0f, 0.0f, 1.0f);
	}else{
		C3D_FVUnifSet(GPU_VERTEX_SHADER, U(u_flip), 0.0f, 1.0f,-1.0f, 0.0f);
	}
}

int
convertPrimitive(PrimitiveType pt, GPU_Primitive_t *prim, u16 **indices, int *numIndex)
{
	u16 *ind = *indices;
	int   ni = *numIndex;
	int i, vi, n = 0;
	switch(pt){
	case PRIMTYPETRIFAN:
	case PRIMTYPETRILIST:
	case PRIMTYPETRISTRIP:
		*prim = primTypeMap[pt];
		break;

	case PRIMTYPEPOLYLINE:
		*prim = GPU_TRIANGLES;
		for(i = 0; i+1 < ni && n+2 < TMPINDEX_NUM; i++, n+=3){
			int vi2;
			vi = ind ? ind[i] : i;
			vi2 = ind ? ind[i+1] : i+1;
			if(vi < 0 || vi >= im3D_vtx_count || vi2 < 0 || vi2 >= im3D_vtx_count)
				return 0;
			tmpIndex[n+0] = vi;
			tmpIndex[n+1] = vi2;
			tmpIndex[n+2] = vi;
		}
		*numIndex = n;
		*indices = tmpIndex;
		break;
		
	case PRIMTYPELINELIST:
		*prim = GPU_TRIANGLES;
		for(i = 0; i+1 < ni && n+2 < TMPINDEX_NUM; i+=2, n+=3){
			int vi2;
			vi = ind ? ind[i] : i;
			vi2 = ind ? ind[i+1] : i+1;
			if(vi < 0 || vi >= im3D_vtx_count || vi2 < 0 || vi2 >= im3D_vtx_count)
				return 0;
			tmpIndex[n+0] = vi;
			tmpIndex[n+1] = vi2;
			tmpIndex[n+2] = vi;
		}
		*numIndex = n;
		*indices = tmpIndex;
		break;

	case PRIMTYPEPOINTLIST:
		*prim = GPU_TRIANGLES;
		for(i = 0; i < ni && n+2 < TMPINDEX_NUM; i++, n+=3){
			vi = ind ? ind[i] : i;
			if(vi < 0 || vi >= im3D_vtx_count)
				return 0;
			tmpIndex[n+0] = vi;
			tmpIndex[n+1] = vi;
			tmpIndex[n+2] = vi;
		}
		*numIndex = n;
		*indices = tmpIndex;
		break;
		
	case PRIMTYPENONE:
	default:
		return 0;
	}

	return 1;
}
	
void
im2DRenderIndexedPrimitive(PrimitiveType primType,
	void *vertices, int32 numVertices,
	void *indices, int32 numIndices)
{
	u16 *ind = (u16*)indices;
	Im2DVertex *vtx = (Im2DVertex*)vertices;
	int i, j, vi, ni = indices ? numIndices : numVertices;
	GPU_Primitive_t prim;

	if(!convertPrimitive(primType, &prim, &ind, &ni)){
		return;
	}

	if(im2dOverrideShader)
		im2dOverrideShader->use();
	else
		im2dShader->use();

	flushCache();
	im2DSetXform();
	C3D_SetAttrInfo(&im2DVao);

	C3D_ImmDrawBegin(prim);
	for(i = 0; i < ni; i++){
		vi = indices ? ind[i] : i;
		C3D_ImmSendAttrib(vtx[vi].x, vtx[vi].y, vtx[vi].z, vtx[vi].w);
		C3D_ImmSendAttrib((float)vtx[vi].r/255.0f,
				  (float)vtx[vi].g/255.0f,
				  (float)vtx[vi].b/255.0f,
				  (float)vtx[vi].a/255.0f);
		C3D_ImmSendAttrib(vtx[vi].u, vtx[vi].v, 0, 0);
	}
	C3D_ImmDrawEnd();
}

void
im2DRenderPrimitive(PrimitiveType primType, void *vertices, int32 numVertices)
{
	im2DRenderIndexedPrimitive(primType, vertices, numVertices, NULL, 0);
}

void
im2DRenderBlit()
{
	int i, vi;

	/* あああああああああああああああ!!!!!!!!!!! */
	/* はく こづまんき ぶるして */
	
	float v0 = 16.0f / 256.0f;
	float v1 = 1.0f + v0;
	
	float vtx[4][6] = {
		{ 0, v0,-1, 1, 0, 1 },
		{ 1, v0,-1, 1, 0, 0 },
		{ 1, v1,-1, 1, 1, 0 },
		{ 0, v1,-1, 1, 1, 1 },
	};

	u16 ind[6] = {
		0, 1, 2,
		0, 2, 3
	};

	im2dShader->use();
	flushCache();

	C3D_FVUnifSet(GPU_VERTEX_SHADER, U(u_xform), 2.0f, -2.0, -1, 1);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, U(u_flip), 1.0f, 0.0f, 0.0f, 1.0f);
	
	C3D_SetAttrInfo(&im2DVao);
	C3D_ImmDrawBegin(GPU_TRIANGLES);
	for(i = 0; i < 6; i++){
		vi = ind[i];
		C3D_ImmSendAttrib(vtx[vi][0], vtx[vi][1], vtx[vi][2], vtx[vi][3]);
		C3D_ImmSendAttrib(255,        255,        255,        255);
		C3D_ImmSendAttrib(vtx[vi][4], vtx[vi][5], 0,          0);
	}
	C3D_ImmDrawEnd();
}
  
void
openIm3D(void)
{
	im3dShader = Shader::create(VSH_PRG_IM3D, combiner_simple, false);
	assert(im3dShader);
	AttrInfo_Init(&im3DVao);
	AttrInfo_AddLoader(&im3DVao, 0, GPU_FLOAT, 4); // in_pos / v0
	AttrInfo_AddLoader(&im3DVao, 1, GPU_FLOAT, 4); // in_col / v1
	AttrInfo_AddLoader(&im3DVao, 2, GPU_FLOAT, 2); // in_tc0 / v2
#ifdef RW_3DS_BUFFERED_IM3D
	/* Large water batches must not be expanded into thousands of PICA
	 * immediate attribute commands.  Keep a small per-frame ring of real
	 * vertex/index buffers for callers that explicitly opt in.  The buffered
	 * layout deliberately matches the old immediate path's float attributes,
	 * including normalized RGBA; feeding byte colours directly makes water
	 * saturate to white on PICA200. */
	memset(im3DFrameBuffers, 0, sizeof(im3DFrameBuffers));
	im3DFrameBufferCursor = 0;
	im3DBuffered = false;
#endif
}

void
closeIm3D(void)
{
#ifdef RW_3DS_BUFFERED_IM3D
	for(int32 i = 0; i < IM3D_FRAME_BUFFER_COUNT; i++){
		if(im3DFrameBuffers[i])
			linearFree(im3DFrameBuffers[i]);
		im3DFrameBuffers[i] = nil;
	}
#endif
	im3dShader->destroy();
	im3dShader = nil;
}

void
setIm3DBuffered(bool32 enable)
{
#ifdef RW_3DS_BUFFERED_IM3D
	im3DBuffered = enable;
#else
	(void)enable;
#endif
}

void
resetIm3DFrameBuffers(void)
{
#ifdef RW_3DS_BUFFERED_IM3D
	im3DFrameBufferCursor = 0;
	im3DBuffered = false;
#endif
}

void
im3DTransform(void *vertices, int32 numVertices, Matrix *world, uint32 flags)
{
	if(world == nil){
		static Matrix ident;
		ident.setIdentity();
		world = &ident;
	}

	setWorldMatrix(world);

	if((flags & im3d::VERTEXUV) == 0){
		SetRenderStatePtr(TEXTURERASTER, nil);
	}

	im3D_vtx = (Im3DVertex*)vertices;
	im3D_vtx_count = numVertices;
}
	
void
im3DRenderIndexedPrimitive(PrimitiveType primType, void *indices, int32 numIndices)
{
	Im3DVertex *vtx = im3D_vtx;
	u16 *ind = (u16*)indices;
	int ni = indices ? numIndices : im3D_vtx_count;
	GPU_Primitive_t prim;
	int i, vi;

	if(!convertPrimitive(primType, &prim, &ind, &ni)){
		return;
	}	

#ifdef RW_3DS_BUFFERED_IM3D
	if(im3DBuffered && im3DFrameBufferCursor < IM3D_FRAME_BUFFER_COUNT &&
	   im3D_vtx_count <= IM3D_FRAME_MAX_VERTICES && ni <= IM3D_FRAME_MAX_INDICES){
		int32 slot = im3DFrameBufferCursor;
		uint8 *storage = acquireIm3DFrameBuffer(slot);
		if(storage == nil){
			/* Avoid retrying the same failed allocation for every remaining water
			 * batch this frame; the code below safely falls back to immediate mode. */
			im3DFrameBufferCursor = IM3D_FRAME_BUFFER_COUNT;
		}else{
			im3DFrameBufferCursor++;
			BufferedIm3DVertex *vbuf = (BufferedIm3DVertex*)storage;
			u16 *ibuf = (u16*)(storage +
				IM3D_FRAME_MAX_VERTICES * sizeof(BufferedIm3DVertex));
		for(i = 0; i < im3D_vtx_count; i++){
			vbuf[i].x = vtx[i].position.x;
			vbuf[i].y = vtx[i].position.y;
			vbuf[i].z = vtx[i].position.z;
			vbuf[i].w = 1.0f;
			vbuf[i].r = (float)vtx[i].r / 255.0f;
			vbuf[i].g = (float)vtx[i].g / 255.0f;
			vbuf[i].b = (float)vtx[i].b / 255.0f;
			vbuf[i].a = (float)vtx[i].a / 255.0f;
			vbuf[i].u = vtx[i].u;
			vbuf[i].v = vtx[i].v;
		}
		if(ind)
			memcpy(ibuf, ind, ni * sizeof(u16));

		C3D_BufInfo vbo;
		BufInfo_Init(&vbo);
		/* Attribute 0 -> v0 position, 1 -> v1 colour, 2 -> v2 UV. */
		int vboResult = BufInfo_Add(&vbo, vbuf, sizeof(BufferedIm3DVertex), 3, 0x210);
		assert(vboResult >= 0);

		im3dShader->use();
		flushCache();
		C3D_SetAttrInfo(&im3DVao);
		C3D_SetBufInfo(&vbo);
		if(ind)
			C3D_DrawElements(prim, ni, C3D_UNSIGNED_SHORT, ibuf);
		else
			C3D_DrawArrays(prim, 0, ni);
		return;
		}
	}
#endif

	im3dShader->use();
	flushCache();
	C3D_SetAttrInfo(&im3DVao);

	C3D_ImmDrawBegin(prim);
	for(i = 0; i < ni; i++){
		vi = ind ? ind[i] : i;
		C3D_ImmSendAttrib(vtx[vi].position.x,
				  vtx[vi].position.y,
				  vtx[vi].position.z,
				  1.0f);
		C3D_ImmSendAttrib((float)vtx[vi].r/255.0f,
				  (float)vtx[vi].g/255.0f,
				  (float)vtx[vi].b/255.0f,
				  (float)vtx[vi].a/255.0f);
		C3D_ImmSendAttrib(vtx[vi].u, vtx[vi].v, 0, 0);
	}
	C3D_ImmDrawEnd();
}

void
im3DRenderPrimitive(PrimitiveType primType)
{
	im3DRenderIndexedPrimitive(primType, NULL, 0);
}

void
im3DEnd(void)
{
}

}
}

#endif
