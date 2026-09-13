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

static inline bool
isVehicleDepthOffsetTexture(Texture *tex)
{
#ifdef RE3_3DS_BUILD
	/* Only independent overlay materials verified in the stock DFFs belong
	 * here.  Never match the shared *8bit128 body/detail atlases: doing so moves
	 * lamps, trim and body panels and visibly breaks the whole vehicle. */
	if(tex == nil)
		return false;
	const char *name = tex->name;
	const char *mask = tex->mask;
	return strcmp(name, "taxi64") == 0 ||
	       strcmp(name, "ambudecals128") == 0 ||
	       strcmp(mask, "coachdecals4bit128") == 0 ||
	       strcmp(name, "poldecals128") == 0 ||
	       strcmp(name, "lcpdbadge4bit64") == 0 ||
	       strcmp(name, "badges64") == 0 ||
	       strcmp(name, "lcpd4bit64aback") == 0 ||
	       strcmp(mask, "fdlc128") == 0 ||
	       strncmp(mask, "numders", 7) == 0 ||
	       strncmp(name, "mrwhoopdecals", 15) == 0 ||
	       strcmp(name, "mrwongsdecal4bit128") == 0 ||
	       strcmp(mask, "mulesigns4bit256a") == 0 ||
	       strcmp(mask, "panlantic_128") == 0 ||
	       strcmp(mask, "armour_logosa_128") == 0 ||
	       strcmp(mask, "toyz_128") == 0 ||
	       strcmp(mask, "yankeesigns4bit256a") == 0;
#elif defined(RESTORIES_3DS_BUILD)
	/* LCS inherits many of re3's independent badge/decal meshes and adds a
	 * shared xv_ plate material.  Bias only these verified overlay families;
	 * this callback also sees world atomics, so never bias a whole body atlas. */
	if(tex == nil)
		return false;
	const char *name = tex->name;
	const char *mask = tex->mask;
	return strncmp(name, "plates", 6) == 0 ||
	       strcmp(name, "xv_licenseplates") == 0 ||
	       strcmp(name, "licenseplates") == 0 ||
	       strcmp(name, "taxi64") == 0 ||
	       strcmp(name, "ambudecals128") == 0 ||
	       strcmp(name, "coachdecals4bit128") == 0 ||
	       strcmp(mask, "coachdecals4bit128") == 0 ||
	       strcmp(name, "poldecals128") == 0 ||
	       strcmp(name, "lcpdbadge4bit64") == 0 ||
	       strcmp(name, "badges64") == 0 ||
	       strcmp(name, "lcpd4bit64aback") == 0 ||
	       strcmp(name, "polmavdecals128") == 0 ||
	       strcmp(mask, "fdlc128") == 0 ||
	       strncmp(mask, "numders", 7) == 0 ||
	       strncmp(name, "mrwhoopdecals", 15) == 0 ||
	       strcmp(name, "mrwongsdecal4bit128") == 0 ||
	       strcmp(name, "ib_mulesigns") == 0 ||
	       strcmp(mask, "mulesigns4bit256a") == 0 ||
	       strcmp(mask, "panlantic_128") == 0 ||
	       strcmp(mask, "armour_logosa_128") == 0 ||
	       strcmp(mask, "toyz_128") == 0 ||
	       strcmp(name, "ib_yankeesigns") == 0 ||
	       strcmp(mask, "yankeesigns4bit256a") == 0;
#else
	/* Not every VC vehicle uses MatFX.  The remaining plate materials pass
	 * through defaultRenderCB, so give plates and the near-coplanar Hotring
	 * sponsor/number meshes the same small separation as the MatFX renderer.
	 * Keep this an exact family whitelist: biasing the whole vehicle would move
	 * glass, lamps and body panels relative to one another. */
	if(tex == nil)
		return false;
	const char *name = tex->name;
	const char *mask = tex->mask;
	return strncmp(name, "plates", 6) == 0 ||
	       strncmp(name, "hotringad", 9) == 0 ||
	       strncmp(name, "hotrinaad", 9) == 0 ||
	       strncmp(name, "hotrinbad", 9) == 0 ||
	       strncmp(name, "hotrinadv", 9) == 0 ||
	       strncmp(name, "hotrinanum", 10) == 0 ||
	       strncmp(name, "hotrinbnum", 10) == 0 ||
	       strcmp(name, "ambudecals128") == 0 ||
	       strcmp(mask, "bensonsigns4bit256") == 0 ||
	       strcmp(mask, "bobcatlogo") == 0 ||
	       strcmp(name, "boxville864bit256signs") == 0 ||
	       strcmp(name, "chopper86decals128a") == 0 ||
	       strcmp(mask, "coach86decals4bit128") == 0 ||
	       strcmp(name, "vcpoldecals128") == 0 ||
	       strcmp(name, "vcpdbadge4bit64") == 0 ||
	       strcmp(name, "vcfd8bit128a") == 0 ||
	       strncmp(name, "kaufmandecal", 12) == 0 ||
	       strcmp(name, "mrwhoop86decals128") == 0 ||
	       strcmp(mask, "mulesigns4bit256a") == 0 ||
	       strcmp(name, "policedecals64") == 0 ||
	       strcmp(name, "polmavdecals128a") == 0 ||
	       strcmp(name, "rumpo864bit256signs") == 0 ||
	       strcmp(mask, "securica86logos128") == 0 ||
	       strcmp(name, "spandsign8bit128b") == 0 ||
	       strncmp(mask, "vcnmavlogo", 10) == 0 ||
	       strcmp(mask, "vcnmavdecal") == 0 ||
	       strcmp(name, "yankee864bit256signs") == 0;
#endif
}

void
drawInst_simple(InstanceDataHeader *header, InstanceData *inst)
{
	flushCache();
	C3D_DrawElements(header->primType,
			 inst->numIndex,
			 C3D_UNSIGNED_SHORT,
			 inst->indexBuffer);
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

RGBAf
getAtomicAmbientLight(Atomic *atomic)
{
	WorldLights lightData;
	/* MatFX paint only uses the accumulated ambient colour.  Give
	 * enumerateLights zero directional/local capacity so it cannot perform the
	 * per-atomic local-light sphere tests or prepare unused light uniforms. */
	lightData.directionals = nil;
	lightData.locals = nil;
	lightData.numDirectionals = 0;
	lightData.numLocals = 0;
	if(atomic->geometry->flags & rw::Geometry::LIGHT)
		((World*)engine->currentWorld)->enumerateLights(atomic, &lightData);
	else{
		memset(&lightData, 0, sizeof(lightData));
		lightData.ambient.alpha = 1.0f;
	}
	return lightData.ambient;
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
		bool depthOffset = isVehicleDepthOffsetTexture(m->texture);
		if(depthOffset)
			C3D_DepthMap(true, -1.0f, 0.0030f);
		drawInst(header, inst);
		if(depthOffset)
			C3D_DepthMap(true, -1.0f, 0.0f);
		inst++;
	}
}

}
}

#endif
