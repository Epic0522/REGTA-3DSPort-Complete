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
	im2dShader = Shader::create(VSH_PRG_IM2D, combiner_simple);
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

int forceTiltOff = 0;
	
void
im2DSetXform(void)
{
	Camera *cam = (Camera*)engine->currentCamera;
	int tilt = cameraTilt(cam);
	float w = cam->frameBuffer->width;
	float h = cam->frameBuffer->height;

	C3D_FVUnifSet(GPU_VERTEX_SHADER, U(u_xform), 2.0f/w, -2.0/h, -1, 1);
	
	if(!tilt || forceTiltOff){
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
		for(i = 0; i < ni && n+2 < TMPINDEX_NUM; i++, n+=3){
			vi = ind ? ind[i] : i;
			tmpIndex[n+0] = ind[vi];
			tmpIndex[n+1] = ind[vi+1];
			tmpIndex[n+2] = ind[vi];
		}
		*numIndex = n;
		*indices = tmpIndex;
		break;
		
	case PRIMTYPELINELIST:
		*prim = GPU_TRIANGLES;
		for(i = 0; i < ni && n + 2 < TMPINDEX_NUM; i+=2, n+=3){
			vi = ind ? ind[i] : i;
			tmpIndex[n+0] = ind[vi];
			tmpIndex[n+1] = ind[vi+1];
			tmpIndex[n+2] = ind[vi];
		}
		*numIndex = n;
		*indices = tmpIndex;
		break;

	case PRIMTYPEPOINTLIST:
		*prim = GPU_TRIANGLES;
		for(i = 0; i < ni && n + 2 < TMPINDEX_NUM; i++, n+=3){
			vi = ind ? ind[i] : i;
			tmpIndex[n+0] = ind[vi];
			tmpIndex[n+1] = ind[vi];
			tmpIndex[n+2] = ind[vi];
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

int rasterStageCamera();
	
void
im2DRenderIndexedPrimitive(PrimitiveType primType,
	void *vertices, int32 numVertices,
	void *indices, int32 numIndices)
{
	u16 *ind = (u16*)indices;
	Im2DVertex *vtx = (Im2DVertex*)vertices;
	int i, j, vi, si, ni = indices ? numIndices : numVertices;
	GPU_Primitive_t prim;

	/* a dirty hack(?) for rendering camera textures in 3d.
	   Not very useful, but I'm just curious how it looks. */
	int cameraTex = rasterStageCamera();
	
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

	for(si = 0; si < 2; si++){
		if(si == 0){
			cameraLHS();
		}else{
			cameraRHS();
		}
		
		C3D_ImmDrawBegin(prim);
		for(i = 0; i < ni; i++){
			vi = indices ? ind[i] : i;
			C3D_ImmSendAttrib(vtx[vi].x, vtx[vi].y, vtx[vi].z, vtx[vi].w);
			C3D_ImmSendAttrib((float)vtx[vi].r/255.0f,
					  (float)vtx[vi].g/255.0f,
					  (float)vtx[vi].b/255.0f,
					  (float)vtx[vi].a/255.0f);

			if(!cameraTex){
				C3D_ImmSendAttrib(vtx[vi].u, vtx[vi].v, 0, 0);
			}else{
				if(!si){
					C3D_ImmSendAttrib(vtx[vi].u / 2 + 0.5, vtx[vi].v, 0, 0);
				}else{
					C3D_ImmSendAttrib(vtx[vi].u / 2, vtx[vi].v, 0, 0);
				}
			}
		}
		C3D_ImmDrawEnd();
	}
}

void
im2DRenderPrimitive(PrimitiveType primType, void *vertices, int32 numVertices)
{
	im2DRenderIndexedPrimitive(primType, vertices, numVertices, NULL, 0);
}

void
im2DRenderBlit(int si)
{
	int i, vi;

	/* あああああああああああああああ!!!!!!!!!!! */
	/* こづまんき ぶるして */
	
	float y0 = 16.0f / 256.0f;
	float y1 = y0 + 1.0f;

	float v0 = (float)si * 0.5f;
	float v1 = v0 + 0.5f;
	
	float vtx[4][6] = {
		{ 0, y0,-1, 1, 0, v1 },
		{ 1, y0,-1, 1, 0, v0 },
		{ 1, y1,-1, 1, 1, v0 },
		{ 0, y1,-1, 1, 1, v1 },
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
	im3dShader = Shader::create(VSH_PRG_IM3D, combiner_simple);
	assert(im3dShader);
	AttrInfo_Init(&im3DVao);
	AttrInfo_AddLoader(&im3DVao, 0, GPU_FLOAT, 4); // in_pos / v0
	AttrInfo_AddLoader(&im3DVao, 1, GPU_FLOAT, 4); // in_col / v1
	AttrInfo_AddLoader(&im3DVao, 2, GPU_FLOAT, 2); // in_tc0 / v2
}

void
closeIm3D(void)
{
	im3dShader->destroy();
	im3dShader = nil;
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
	
static void
im3DRenderIndexedPrimitive(PrimitiveType primType, void *indices, int32 numIndices)
{
	Im3DVertex *vtx = im3D_vtx;
	u16 *ind = (u16*)indices;
	int ni = indices ? numIndices : im3D_vtx_count;
	GPU_Primitive_t prim;
	int i, vi, si;

	if(!convertPrimitive(primType, &prim, &ind, &ni)){
		return;
	}	

	im3dShader->use();
	flushCache();
	C3D_SetAttrInfo(&im3DVao);

	for(si = 0; si < 2; si++){
		if(si == 0){
			cameraLHS();
		}else{
			cameraRHS();
		}
		
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
