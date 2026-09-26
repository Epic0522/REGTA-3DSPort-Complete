#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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

static const float VEHICLE_DECAL_DEPTH_OFFSET = 0.0120f;

static inline bool
vehicleTextureEquals(Texture *tex, const char *value)
{
	return strcasecmp(tex->name, value) == 0 || strcasecmp(tex->mask, value) == 0;
}

static inline bool
vehicleTextureStartsWith(Texture *tex, const char *value, size_t length)
{
	return strncasecmp(tex->name, value, length) == 0 ||
	       strncasecmp(tex->mask, value, length) == 0;
}

static bool
textureNameContains(Texture *tex, const char *needle)
{
	if(tex == nil)
		return false;
	const size_t length = strlen(needle);
	for(const char *text = tex->name; *text; ++text)
		if(strncasecmp(text, needle, length) == 0)
			return true;
	for(const char *text = tex->mask; *text; ++text)
		if(strncasecmp(text, needle, length) == 0)
			return true;
	return false;
}

static bool
isVertexAlphaGlass(InstanceData *inst)
{
#ifdef RESTORIES_3DS_BUILD
	/* The stock LCS assets already encode their pane opacity in vertex and
	 * texture alpha.  Treating names such as gunwin_256 and glasspanel_64 as
	 * VC-style pane materials turns Phil's whole shop frontage into a single
	 * translucent coloured sheet.  Keep LCS on its original material path. */
	(void)inst;
	return false;
#else
	if(!inst->vertexAlpha || inst->material->texture == nil)
		return false;
	C3DRaster *raster = inst->material->texture->raster ?
		GETC3DRASTEREXT(inst->material->texture->raster) : nil;
	/* Authored texture translucency already supplies its own glass opacity.
	 * This path is for opaque textures whose pane opacity lives in vertex
	 * colours, such as Vice City's Downtown Ammu-Nation windows. */
	if(raster && raster->hasTranslucentAlpha)
		return false;
	return textureNameContains(inst->material->texture, "glass") ||
	       textureNameContains(inst->material->texture, "gls") ||
	       textureNameContains(inst->material->texture, "window") ||
	       textureNameContains(inst->material->texture, "gunwin");
#endif
}

static inline bool
isVehicleDepthOffsetTexture(Texture *tex)
{
#ifdef RE3_3DS_BUILD
	/* Only independent overlay materials verified in the stock DFFs belong
	 * here.  Never match the shared *8bit128 body/detail atlases: doing so moves
	 * lamps, trim and body panels and visibly breaks the whole vehicle. */
	if(tex == nil)
		return false;
	return vehicleTextureEquals(tex, "taxi64") ||
	       vehicleTextureEquals(tex, "ambudecals128") ||
	       vehicleTextureEquals(tex, "coachdecals4bit128") ||
	       vehicleTextureEquals(tex, "poldecals128") ||
	       vehicleTextureEquals(tex, "lcpdbadge4bit64") ||
	       vehicleTextureEquals(tex, "lcpdbadge4bit64a") ||
	       vehicleTextureEquals(tex, "lcpd4bit64a") ||
	       vehicleTextureEquals(tex, "lcpd4bit64aback") ||
	       vehicleTextureEquals(tex, "badges64") ||
	       vehicleTextureEquals(tex, "fdlc128") ||
	       vehicleTextureStartsWith(tex, "numders", 7) ||
	       vehicleTextureStartsWith(tex, "mrwhoopdecals", 15) ||
	       vehicleTextureEquals(tex, "mrwongsdecal4bit128") ||
	       vehicleTextureEquals(tex, "mulesigns4bit256a") ||
	       vehicleTextureEquals(tex, "panlantic_128") ||
	       vehicleTextureEquals(tex, "armour_logosa_128") ||
	       vehicleTextureEquals(tex, "toyz_128") ||
	       vehicleTextureEquals(tex, "yankeesigns4bit256a");
#elif defined(RESTORIES_3DS_BUILD)
	/* LCS inherits many of re3's independent badge/decal meshes and adds a
	 * shared xv_ plate material.  Bias only these verified overlay families;
	 * this callback also sees world atomics, so never bias a whole body atlas. */
	if(tex == nil)
		return false;
	return vehicleTextureStartsWith(tex, "plates", 6) ||
	       vehicleTextureEquals(tex, "xv_licenseplates") ||
	       vehicleTextureEquals(tex, "licenseplates") ||
	       vehicleTextureEquals(tex, "taxi64") ||
	       vehicleTextureEquals(tex, "ambudecals128") ||
	       vehicleTextureEquals(tex, "coachdecals4bit128") ||
	       vehicleTextureEquals(tex, "poldecals128") ||
	       vehicleTextureEquals(tex, "lcpdbadge4bit64") ||
	       vehicleTextureEquals(tex, "lcpdbadge4bit64a") ||
	       vehicleTextureEquals(tex, "lcpd4bit64a") ||
	       vehicleTextureEquals(tex, "badges64") ||
	       vehicleTextureEquals(tex, "lcpd4bit64aback") ||
	       vehicleTextureEquals(tex, "polmavdecals128") ||
	       vehicleTextureEquals(tex, "hotroddecal") ||
	       vehicleTextureEquals(tex, "ijb_armourlog") ||
	       vehicleTextureEquals(tex, "ijb_toyz_128") ||
	       vehicleTextureEquals(tex, "xv_badges") ||
	       vehicleTextureEquals(tex, "fdlc128") ||
	       vehicleTextureStartsWith(tex, "numders", 7) ||
	       vehicleTextureStartsWith(tex, "mrwhoopdecals", 15) ||
	       vehicleTextureEquals(tex, "mrwongsdecal4bit128") ||
	       vehicleTextureEquals(tex, "ib_mulesigns") ||
	       vehicleTextureEquals(tex, "mulesigns4bit256a") ||
	       vehicleTextureEquals(tex, "panlantic_128") ||
	       vehicleTextureEquals(tex, "armour_logosa_128") ||
	       vehicleTextureEquals(tex, "toyz_128") ||
	       vehicleTextureEquals(tex, "ib_yankeesigns") ||
	       vehicleTextureEquals(tex, "yankeesigns4bit256a");
#else
	/* Not every VC vehicle uses MatFX.  The remaining plate materials pass
	 * through defaultRenderCB, so give plates and the near-coplanar Hotring
	 * sponsor/number meshes the same small separation as the MatFX renderer.
	 * Keep this an exact family whitelist: biasing the whole vehicle would move
	 * glass, lamps and body panels relative to one another. */
	if(tex == nil)
		return false;
	return vehicleTextureStartsWith(tex, "plates", 6) ||
	       vehicleTextureStartsWith(tex, "hotringad", 9) ||
	       vehicleTextureStartsWith(tex, "hotrinaad", 9) ||
	       vehicleTextureStartsWith(tex, "hotrinbad", 9) ||
	       vehicleTextureStartsWith(tex, "hotrinadv", 9) ||
	       vehicleTextureStartsWith(tex, "hotrinanum", 10) ||
	       vehicleTextureStartsWith(tex, "hotrinbnum", 10) ||
	       vehicleTextureEquals(tex, "ambudecals128") ||
	       vehicleTextureEquals(tex, "bensonsigns4bit256") ||
	       vehicleTextureEquals(tex, "bobcatlogo") ||
	       vehicleTextureEquals(tex, "boxville864bit256signs") ||
	       vehicleTextureEquals(tex, "chopper86decals128a") ||
	       vehicleTextureEquals(tex, "chopper86decals128") ||
	       vehicleTextureEquals(tex, "coach86decals4bit128") ||
	       vehicleTextureEquals(tex, "vcpoldecals128") ||
	       vehicleTextureEquals(tex, "vcpdbadge4bit64") ||
	       vehicleTextureEquals(tex, "vcfd8bit128a") ||
	       vehicleTextureStartsWith(tex, "kaufmandecal", 12) ||
	       vehicleTextureEquals(tex, "mrwhoop86decals128") ||
	       vehicleTextureEquals(tex, "mulesigns4bit256a") ||
	       vehicleTextureEquals(tex, "policedecals64") ||
	       vehicleTextureEquals(tex, "polmavdecals128a") ||
	       vehicleTextureEquals(tex, "polmavdecals128") ||
	       vehicleTextureStartsWith(tex, "numders", 7) ||
	       vehicleTextureEquals(tex, "rumpo864bit256signs") ||
	       vehicleTextureEquals(tex, "securica86logos128") ||
	       vehicleTextureEquals(tex, "spandsign8bit128b") ||
	       vehicleTextureEquals(tex, "spandsign8bit128a") ||
	       vehicleTextureStartsWith(tex, "vcnmavlogo", 10) ||
	       vehicleTextureEquals(tex, "vcnmavdecal") ||
	       vehicleTextureEquals(tex, "yankee864bit256signs");
#endif
}

static inline bool
isVehicleDepthOffsetMaterial(Material *material)
{
	if(material == nil) return false;
	if(isVehicleDepthOffsetTexture(material->texture)) return true;
	// Kaufman's horizontal black body stripe is an independent untextured
	// overlay, so it cannot be identified by the texture whitelist above.
	const RGBA &color = material->color;
	return getEntityRenderStyle().untexturedBlackDecal && material->texture == nil &&
	       color.red == 0 && color.green == 0 && color.blue == 0 && color.alpha == 255;
}

// Stage 5 is reserved for entity fading, after material, vertex and texture
// alpha. This also covers plain interior meshes and detached/damage atomics
// that do not share the bodywork shader. No material mutation or extra draw.
static void
applyEntityOpacity(void)
{
	static bool wasFaded = false;
	const float opacity = getEntityRenderStyle().opacity;
	if(opacity >= 1.f && !wasFaded) return;
	C3D_TexEnv env;
	C3D_TexEnvInit(&env);
	if(opacity < 1.f) {
		C3D_TexEnvSrc(&env, C3D_RGB, GPU_PREVIOUS);
		C3D_TexEnvFunc(&env, C3D_RGB, GPU_REPLACE);
		C3D_TexEnvSrc(&env, C3D_Alpha, GPU_PREVIOUS, GPU_CONSTANT);
		C3D_TexEnvFunc(&env, C3D_Alpha, GPU_MODULATE);
		const uint32 alpha = (uint32)(opacity * 255.f);
		C3D_TexEnvColor(&env, (alpha << 24) | 0x00FFFFFF);
	}
	C3D_SetTexEnv(5, &env);
	wasFaded = opacity < 1.f;
}

static uint32 worldLightClusterMask = 0xFFFFFFFF;

void
setWorldLightClusterMask(uint32 mask)
{
	worldLightClusterMask = mask;
}

static void
drawInstElements(InstanceDataHeader *header, InstanceData *inst, ProfileDrawClass drawClass)
{
	// sublightsb contains one triangle-list mesh whose five building groups are
	// contiguous: flat26/27/28/29 use 96 triangles each and towernew1 uses 120.
	// The mask is set only while that atomic renders.  Fall back untouched for
	// any asset variant whose layout does not match the stock 1512 indices.
	if(worldLightClusterMask == 0xFFFFFFFF || header->primType != GPU_TRIANGLES || inst->numIndex != 1512) {
		C3D_DrawElements(header->primType, inst->numIndex, C3D_UNSIGNED_SHORT, inst->indexBuffer);
		profileRecordDraw(inst->numIndex, drawClass);
		return;
	}
	static const uint16 firstIndex[5] = {0, 288, 576, 864, 1152};
	static const uint16 indexCount[5] = {288, 288, 288, 288, 360};
	for(uint32 cluster = 0; cluster < 5; cluster++) {
		if((worldLightClusterMask & (1u << cluster)) == 0) continue;
		C3D_DrawElements(GPU_TRIANGLES, indexCount[cluster], C3D_UNSIGNED_SHORT, inst->indexBuffer + firstIndex[cluster]);
		profileRecordDraw(indexCount[cluster], drawClass);
	}
}

void
drawInst_simple(InstanceDataHeader *header, InstanceData *inst, ProfileDrawClass drawClass)
{
	applyEntityOpacity();
	flushCache();
	if(!stereoRenderActive()){
		drawInstElements(header, inst, drawClass);
		return;
	}
	const int32 passes = stereoRenderPassCount();
	for(int32 pass = 0; pass < passes; pass++){
		const int32 eye = stereoRenderEye(pass);
		setStereoEye(eye);
		drawInstElements(header, inst, drawClass);
	}
	setStereoEye(stereoRenderEye(0));
}

// A transparent material is not just a cutout fringe: glass can live entirely
// below the GS alpha reference. Vehicle textures may carry that alpha too.
static bool
needsBlendedAlphaPass(InstanceData *inst, ProfileDrawClass drawClass)
{
	Texture *texture = inst->material->texture;
	const bool translucentTexture = texture && texture->raster &&
		GETC3DRASTEREXT(texture->raster)->hasTranslucentAlpha;
	return drawClass == PROFILE_DRAW_MATFX || inst->vertexAlpha ||
		inst->material->color.alpha != 255 || translucentTexture;
}

// Emulate PS2 GS alpha test FB_ONLY case: failed alpha writes to frame- but not to depth buffer
void
drawInst_GSemu(InstanceDataHeader *header, InstanceData *inst, ProfileDrawClass drawClass)
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
			if((stereoRenderActive() || performanceModeActive()) && !needsBlendedAlphaPass(inst, drawClass)){
				/* The PS2 FB_ONLY emulation normally submits alpha-tested
				 * geometry twice.  In stereo that becomes four draws, mostly
				 * to preserve a soft fringe only a pixel wide on the 3DS.
				 * Keep the depth-writing cutout pass and omit the fringe pass. */
				SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAGREATEREQUAL);
				SetRenderState(rw::ALPHATESTREF, gsalpharef);
				drawInst_simple(header, inst, drawClass);
				SetRenderState(rw::ALPHATESTFUNC, alphafunc);
				SetRenderState(rw::ALPHATESTREF, alpharef);
				return;
			}

			SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAGREATEREQUAL);
			SetRenderState(rw::ALPHATESTREF, gsalpharef);
			drawInst_simple(header, inst, drawClass);
			SetRenderState(rw::ALPHATESTFUNC, rw::ALPHALESS);
			SetRenderState(rw::ZWRITEENABLE, 0);
			drawInst_simple(header, inst, drawClass);
			SetRenderState(rw::ZWRITEENABLE, 1);
			SetRenderState(rw::ALPHATESTFUNC, alphafunc);
			SetRenderState(rw::ALPHATESTREF, alpharef);
		}else{
			SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAALWAYS);
			drawInst_simple(header, inst, drawClass);
			SetRenderState(rw::ALPHATESTFUNC, alphafunc);
		}
	}else
		drawInst_simple(header, inst, drawClass);
}

void
drawInst(InstanceDataHeader *header, InstanceData *inst, ProfileDrawClass drawClass)
{
	const float opacity = getEntityRenderStyle().opacity;
	if(opacity <= 0.f) return;
	if(opacity < 1.f){
		const uint32 alpha = GetRenderState(VERTEXALPHA);
		const uint32 func = GetRenderState(ALPHATESTFUNC);
		const uint32 ref = GetRenderState(ALPHATESTREF);
		SetRenderState(VERTEXALPHA,1);
		SetRenderState(ALPHATESTFUNC,ALPHAGREATEREQUAL);
		const uint32 cutoff = GetRenderState(GSALPHATEST) ? GetRenderState(GSALPHATESTREF) : ref;
		const uint32 fadedCutoff = (uint32)(cutoff*opacity) > 0 ? (uint32)(cutoff*opacity) : 1;
		SetRenderState(ALPHATESTREF, fadedCutoff);
		if(GetRenderState(GSALPHATEST) && needsBlendedAlphaPass(inst, drawClass)){
			// Scale the GS split with the entity fade, retaining glass below it.
			const uint32 gsRef = GetRenderState(GSALPHATESTREF);
			SetRenderState(GSALPHATESTREF, fadedCutoff);
			drawInst_GSemu(header,inst,drawClass);
			SetRenderState(GSALPHATESTREF, gsRef);
		}else
			drawInst_simple(header,inst,drawClass);
		SetRenderState(VERTEXALPHA,alpha);
		SetRenderState(ALPHATESTFUNC,func);
		SetRenderState(ALPHATESTREF,ref);
		return;
	}
	if(rw::GetRenderState(rw::GSALPHATEST)){
		drawInst_GSemu(header, inst, drawClass);
	}else{
		drawInst_simple(header, inst, drawClass);
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
	
static C3D_AttrInfo cachedVertexLayout;
static bool vertexLayoutValid;

void
resetVertexLayoutCache(void)
{
	vertexLayoutValid = false;
}

void
setVertexBuffer(C3D_BufInfo *buffer)
{
	C3D_SetBufInfo(buffer);
}

void
setVertexLayout(C3D_AttrInfo *layout)
{
	// Compare values, not geometry pointers: most neighbouring world atomics
	// have the same layout. All immediate paths use this cache as well.
	if(!vertexLayoutValid || memcmp(&cachedVertexLayout, layout, sizeof(*layout)) != 0){
		C3D_SetAttrInfo(layout);
		cachedVertexLayout = *layout;
		vertexLayoutValid = true;
	}
}

void
setAttribPointers(InstanceDataHeader *header)
{
	setVertexLayout(&header->vao);
	setVertexBuffer(&header->vbo);
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
	lightData.directionals = directionals;
	lightData.numDirectionals = 8;
	/* The 3DS default shader only consumes ambient and directional lights; the
	 * local point/spot loop in setLights is disabled.  Asking World to collect
	 * eight local lights for every atomic therefore performs all sphere tests
	 * and list walking, then discards the result.  This is especially expensive
	 * for GTA III's many tiny atomics and is unrelated to the separate dynamic
	 * vehicle-shadow light list. */
	lightData.locals = nil;
	lightData.numLocals = 0;

	if(atomic->geometry->flags & rw::Geometry::LIGHT){
		((World*)engine->currentWorld)->enumerateLights(atomic, &lightData);
		if((atomic->geometry->flags & rw::Geometry::NORMALS) == 0){
			// Get rid of lights that need normals when we don't have any
			lightData.numDirectionals = 0;
			lightData.numLocals = 0;
		}
		setLights(&lightData);
		return lightData.numDirectionals;
	}else{
		memset(&lightData, 0, sizeof(lightData));
		setLights(&lightData);
		return 0;
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
	if(header->vegetationProxy == -2)
		header->vegetationProxy = findVegetationProxy(atomic->geometry);
	const float canopyBlend = vegetationBlend(atomic, header->vegetationProxy);
	// Resolve once for both eyes; do not make independent per-eye visibility
	// decisions. The split mask still records all early-pass responsibilities.
	const uint32 oldZWrite = GetRenderState(ZWRITEENABLE);
	const uint32 oldAlphaFunc = GetRenderState(ALPHATESTFUNC);
	const uint32 oldAlphaRef = GetRenderState(ALPHATESTREF);
	setWorldMatrix(atomic->getFrame()->getLTM());
	
	lightingCB(atomic);

	setAttribPointers(header);
	// Keep each eye's material order, but bind its framebuffer only once per
	// multi-material atomic. End the scope before the separate canopy proxy.
	InstanceData *inst = header->inst;
	int32 n = header->numMeshes;

	defaultShader->use();

	while(n--){
		m = inst->material;
		const bool canopy = canopyBlend > 0.f && vegetationMaterial(atomic->geometry, header->vegetationProxy, m);
		if(canopy && canopyBlend >= 1.f){ inst++; continue; }
		RGBA color=m->color;
		uint32 materialFlags = flags;
		if(isVertexAlphaGlass(inst) && inst->maxVertexAlpha > 128){
			/* Cap the pane itself at roughly 50% after vertex and material alpha
			 * are multiplied.  Do not flatten gradients or texture-authored glass. */
			const uint32 multiplier = (128u*255u + inst->maxVertexAlpha/2u) /
				inst->maxVertexAlpha;
			color.alpha = (uint8)(uint32(color.alpha)*multiplier/255u);
			materialFlags |= Geometry::MODULATE;
		}
		if(canopy){
			color.alpha=(uint8)(color.alpha*(1.f-canopyBlend));
			SetRenderState(ZWRITEENABLE,0);
			SetRenderState(ALPHATESTFUNC,ALPHAGREATEREQUAL);
			SetRenderState(ALPHATESTREF,3);
		}
		setMaterial(canopy ? materialFlags | Geometry::MODULATE : materialFlags, color, m->surfaceProps);
		setTexture(0, m->texture);
		rw::SetRenderState(VERTEXALPHA, inst->vertexAlpha || color.alpha != 0xFF);
		bool depthOffset = isVehicleDepthOffsetMaterial(m);
		if(depthOffset)
			C3D_DepthMap(true, -1.0f, VEHICLE_DECAL_DEPTH_OFFSET);
		drawInst(header, inst, PROFILE_DRAW_WORLD);
		if(depthOffset)
			C3D_DepthMap(true, -1.0f, 0.0f);
		if(canopy){
			SetRenderState(ZWRITEENABLE,oldZWrite);
			SetRenderState(ALPHATESTFUNC,oldAlphaFunc);
			SetRenderState(ALPHATESTREF,oldAlphaRef);
		}
		inst++;
	}
	if(canopyBlend>0.f){
		if(canopyBlend<1.f) SetRenderState(ZWRITEENABLE,0);
		renderVegetationProxy(atomic,header->vegetationProxy,canopyBlend);
		SetRenderState(ZWRITEENABLE,oldZWrite);
	}
}

}
}

#endif
