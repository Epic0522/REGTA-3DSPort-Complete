#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../rwbase.h"
#include "../rwerror.h"
#include "../rwplg.h"
#include "../rwrender.h"
#include "../rwengine.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#ifdef RW_3DS
#include "rw3ds.h"
#include "rw3dsimpl.h"
#include "rw3dsshader.h"

namespace rw {
namespace c3d {

void
drawInst_simple(InstanceDataHeader *header, InstanceData *inst)
{
	flushCache();
	cameraLHS();
	C3D_DrawElements(header->primType, inst->numIndex, C3D_UNSIGNED_SHORT, inst->indexBuffer);
	cameraRHS();
	C3D_DrawElements(header->primType, inst->numIndex, C3D_UNSIGNED_SHORT, inst->indexBuffer);
}

// Emulate PS2 GS alpha test FB_ONLY case: failed alpha writes to frame- but not to depth buffer
void
drawInst_GSemu(InstanceDataHeader *header, InstanceData *inst)
{
	uint32 hasAlpha;
	int alphafunc, alpharef, gsalpharef;
	int zwrite;
	hasAlpha = getAlphaBlend();
	if(hasAlpha){
		zwrite = rw::GetRenderState(rw::ZWRITEENABLE);
		alphafunc = rw::GetRenderState(rw::ALPHATESTFUNC);
		if(zwrite){
			alpharef = rw::GetRenderState(rw::ALPHATESTREF);
			gsalpharef = rw::GetRenderState(rw::GSALPHATESTREF);

			SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAGREATEREQUAL);
			SetRenderState(rw::ALPHATESTREF, gsalpharef);
			drawInst_simple(header, inst);
			SetRenderState(rw::ALPHATESTFUNC, rw::ALPHALESS);
			SetRenderState(rw::ZWRITEENABLE, 0);
			drawInst_simple(header, inst);
			SetRenderState(rw::ZWRITEENABLE, 1);
			SetRenderState(rw::ALPHATESTFUNC, alphafunc);
			SetRenderState(rw::ALPHATESTREF, alpharef);
		}else{
			SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAALWAYS);
			drawInst_simple(header, inst);
			SetRenderState(rw::ALPHATESTFUNC, alphafunc);
		}
	}else
		drawInst_simple(header, inst);
}

void
drawInst(InstanceDataHeader *header, InstanceData *inst)
{
	if(rw::GetRenderState(rw::GSALPHATEST)){
		drawInst_GSemu(header, inst);
	}else{
		drawInst_simple(header, inst);
	}
}

void
genAttribPointers(InstanceDataHeader *header)
{
	AttribDesc *a = &header->attribDesc[0];
	u64 reg = 0, perm = 0;
	
	AttrInfo_Init(&header->vao);
	for(reg = 0; reg < MAX_ATTRIBS; reg++, a++){
		if (a->count){
			AttrInfo_AddLoader(&header->vao, reg, a->type, a->count);
			perm |= (reg & 0xf) << (a->index * 4);
		}else{
			AttrInfo_AddFixed(&header->vao, reg);
		}
	}
	
	BufInfo_Init(&header->vbo);
	BufInfo_Add(&header->vbo,
		    header->vertexBuffer,
		    header->stride,
		    header->numAttribs,
		    perm);
}
	
void
setAttribPointers(InstanceDataHeader *header)
{
	C3D_SetAttrInfo(&header->vao);
	C3D_SetBufInfo(&header->vbo);
	// We don't actually need to change this everytime we render
	// but it could be desirable for a different rendering engine.
	// possibly for getting more vector uniforms by moving them into
	// fixed vertex attributes.
	// for(reg = 0; reg < MAX_ATTRIBS; reg++, a++){
	// 	if (!a->count){
	// 		C3D_FixedAttribSet(reg, 0.0, 0.0, 0.0, 1.0);
	// 	}
	// }
}

void
setAttribsFixed(void)
{
	int reg;
	for(reg = 0; reg < MAX_ATTRIBS; reg++){
		if (reg == ATTRIB_COLOR){
			C3D_FixedAttribSet(reg, 0.0, 0.0, 0.0, 255.0);
		}else{
			C3D_FixedAttribSet(reg, 0.0, 0.0, 0.0, 0.0);
		}
	}
}
	
int32
lightingCB(Atomic *atomic)
{
	WorldLights lightData;
	Light *directionals[8];
	Light *locals[8];
	lightData.directionals = directionals;
	lightData.numDirectionals = 8;
	lightData.locals = locals;
	lightData.numLocals = 8;

	if(atomic->geometry->flags & rw::Geometry::LIGHT){
		((World*)engine->currentWorld)->enumerateLights(atomic, &lightData);
		if((atomic->geometry->flags & rw::Geometry::NORMALS) == 0){
			// Get rid of lights that need normals when we don't have any
			lightData.numDirectionals = 0;
			lightData.numLocals = 0;
		}
		return setLights(&lightData);
	}else{
		memset(&lightData, 0, sizeof(lightData));
		return setLights(&lightData);
	}
}


void
defaultRenderCB(Atomic *atomic, InstanceDataHeader *header)
{
	Material *m;

	uint32 flags = atomic->geometry->flags;
	setWorldMatrix(atomic->getFrame()->getLTM());
	
	lightingCB(atomic);

	setAttribPointers(header);
	InstanceData *inst = header->inst;
	int32 n = header->numMeshes;

	defaultShader->use();

	while(n--){
		m = inst->material;
		setMaterial(flags, m->color, m->surfaceProps);
		setTexture(0, m->texture);
		rw::SetRenderState(VERTEXALPHA, inst->vertexAlpha || m->color.alpha != 0xFF);
		drawInst(header, inst);
		inst++;
	}
}

}
}

#endif

