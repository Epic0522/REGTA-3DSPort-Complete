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

#include "default_shbin.h"

#define U(x) (VSH_FVEC_##x)

static Shader *envShader;
static Shader *matfxTextureShader;
#ifdef RESTORIES_3DS_BUILD
static Clump *lcsPlayerVehicleClump;
static Texture *lcsPlayerVehicleReflection;
static Frame *lcsPlayerVehicleReflectionFrame;
static float lcsPlayerVehicleReflectionStrength = 1.0f;
static Texture *lcsTrafficVehicleReflection;
static Frame *lcsTrafficVehicleReflectionFrame;
static Clump *lcsTransitionVehicleClump;
static Texture *lcsTransitionVehicleReflection;
static Frame *lcsTransitionVehicleReflectionFrame;
static float lcsTransitionVehicleReflectionStrength = 1.0f;
#endif

static inline uint32
packTevMaterialColor(const RGBA &color)
{
	return ((uint32)color.alpha << 24) | ((uint32)color.blue << 16) |
	       ((uint32)color.green << 8) | color.red;
}

static inline uint8
packTevChannel(float value)
{
	if(value < 0.0f) value = 0.0f;
	if(value > 255.0f) value = 255.0f;
	return (uint8)value;
}

static inline uint32
packTevReflectionColor(float coefficient, const RGBAf &ambient)
{
	/* This is one TEV constant per material, so it adds no per-pixel lighting
	 * cost.  Only the ambient component is needed; directional/local lighting
	 * is deliberately not prepared for the MatFX vehicle path. */
#ifdef RESTORIES_3DS_BUILD
	/* LCS authors low reflection coefficients (usually 0.12-0.22) and expects
	 * the environment probe to be added at that strength.  Keep a modest
	 * ambient tint, but do not halve the coefficient again: together with the
	 * old PC-side 0.25 scaling that reduced a typical panel to 2.75%, which is
	 * indistinguishable from no reflection on the 3DS screen. */
	float strength = coefficient * 1.25f * 255.0f;
	if(strength > 82.0f) strength = 82.0f;
	uint8 red = packTevChannel((0.75f + ambient.red * 0.25f) * strength);
	uint8 green = packTevChannel((0.75f + ambient.green * 0.25f) * strength);
	uint8 blue = packTevChannel((0.75f + ambient.blue * 0.25f) * strength);
#else
	/* GTA III and Vice City use their authored, static vehicle environment map.
	 * Keep its contribution neutral so the timecycle cannot recolour the whole
	 * car, but retain the proven half-strength 3DS treatment: the authored
	 * coefficients were far too bright when fed to the fixed-function TEV at
	 * full scale and made dark paint look white, like the old 2021 build. */
	(void)ambient;
	uint8 red = packTevChannel(coefficient * 0.5f * 255.0f);
	uint8 green = red;
	uint8 blue = red;
#endif
	return 0xFF000000u | ((uint32)blue << 16) | ((uint32)green << 8) | red;
}

#ifdef RESTORIES_3DS_BUILD
void
setPlayerVehicleClump(Clump *clump)
{
	lcsPlayerVehicleClump = clump;
}

void
setPlayerVehicleReflection(Texture *texture, Frame *captureFrame)
{
	lcsPlayerVehicleReflection = texture;
	lcsPlayerVehicleReflectionFrame = captureFrame;
}

void
setPlayerVehicleReflectionStrength(float strength)
{
	if(strength < 0.0f) strength = 0.0f;
	if(strength > 1.0f) strength = 1.0f;
	lcsPlayerVehicleReflectionStrength = strength;
}

void
setTrafficVehicleReflection(Texture *texture, Frame *captureFrame)
{
	lcsTrafficVehicleReflection = texture;
	lcsTrafficVehicleReflectionFrame = captureFrame;
}

void
setTransitionVehicleReflection(Clump *clump, Texture *texture,
	Frame *captureFrame, float strength)
{
	if(strength < 0.0f) strength = 0.0f;
	if(strength > 1.0f) strength = 1.0f;
	lcsTransitionVehicleClump = clump;
	lcsTransitionVehicleReflection = texture;
	lcsTransitionVehicleReflectionFrame = captureFrame;
	lcsTransitionVehicleReflectionStrength = strength;
}
#endif

static inline bool
isVehicleDepthOffsetTexture(Texture *tex)
{
#ifdef RE3_3DS_BUILD
	/* Match only stock DFF materials that are independent overlays.  The broad
	 * *8bit128 rule was intentionally removed because those atlases are shared
	 * by bodywork, lamps and trim. */
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
	/* Match the same verified LCS overlay materials as the default renderer.
	 * MatFX vehicles take this path, while ordinary vehicles use 3dsrender. */
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
	/* Vice City plates and Hotring sponsor/number meshes sit only a tiny distance
	 * above their backing body polygons.  Match only those texture families so
	 * the offset cannot affect ordinary paint, glass or lamp materials. */
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

static inline bool
textureHasAlpha(Texture *tex)
{
	return tex && tex->raster && GETC3DRASTEREXT(tex->raster)->hasAlpha;
}

static inline void
protectVehicleTexture(Texture *tex)
{
#if defined(RE3_3DS_BUILD)
	/* re3 did not exhibit VC/LCS's vehicle texture LOD damage. */
	(void)tex;
#else
	/* VC and LCS vehicle textures must keep their top mip.  They may still be
	 * unloaded and streamed back normally; the pressure callback now reclaims a
	 * complete unused asset before destructive mip shrinking is considered. */
	if(tex && tex->raster)
		TexSetShrinkProtected(GETC3DRASTEREXT(tex->raster));
#endif
}

#ifdef RESTORIES_3DS_BUILD
static void
combinerLCSPlayerReflection(void)
{
	C3D_TexEnv *env0 = C3D_GetTexEnv(0);
	C3D_TexEnv *env1 = C3D_GetTexEnv(1);
	C3D_TexEnv *env2 = C3D_GetTexEnv(2);
	C3D_TexEnv *env3 = C3D_GetTexEnv(3);
	C3D_TexEnv *env4 = C3D_GetTexEnv(4);
	C3D_TexEnvInit(env0);
	C3D_TexEnvInit(env1);
	C3D_TexEnvInit(env2);
	C3D_TexEnvInit(env3);
	C3D_TexEnvInit(env4);

	/* Preserve the painted vehicle as an independent base layer. */
	C3D_TexEnvSrc(env0, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT);
	C3D_TexEnvFunc(env0, C3D_RGB, GPU_MODULATE);
	C3D_TexEnvSrc(env0, C3D_Alpha, GPU_TEXTURE0, GPU_PRIMARY_COLOR);
	C3D_TexEnvFunc(env0, C3D_Alpha, GPU_MODULATE);
	C3D_TexEnvBufUpdate(C3D_Both, 1 << 0);

	/* A raw 32-pixel camera strip can alternate several saturated signs in
	 * adjacent texels and appear as a rainbow on the stretched bodywork. Blend
	 * it halfway toward neutral grey before extracting the bright features. */
	C3D_TexEnvSrc(env1, C3D_RGB, GPU_TEXTURE1, GPU_CONSTANT, GPU_CONSTANT);
	C3D_TexEnvFunc(env1, C3D_RGB, GPU_INTERPOLATE);
	C3D_TexEnvColor(env1, 0xFF808080u);

	/* Remove broad low/mid-tone ground colour. Bright lane markings, signs,
	 * sky and lit walls survive as subdued reflection features. */
	C3D_TexEnvSrc(env2, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT);
	C3D_TexEnvFunc(env2, C3D_RGB, GPU_SUBTRACT);
	C3D_TexEnvColor(env2, 0xFF484848u);

	/* Fade the surviving feature by surface orientation. */
	C3D_TexEnvSrc(env3, C3D_RGB, GPU_PREVIOUS, GPU_PRIMARY_COLOR);
	C3D_TexEnvFunc(env3, C3D_RGB, GPU_MODULATE);

	/* Thin reflection overlay plus the untouched paint saved after stage 0. */
	C3D_TexEnvSrc(env4, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT,
		GPU_PREVIOUS_BUFFER);
	C3D_TexEnvFunc(env4, C3D_RGB, GPU_MULTIPLY_ADD);
	C3D_TexEnvSrc(env4, C3D_Alpha, GPU_PREVIOUS_BUFFER);
	C3D_TexEnvFunc(env4, C3D_Alpha, GPU_REPLACE);
}

#endif

static void
combinerMatFXTextureOnly(void)
{
	C3D_TexEnv *env0 = C3D_GetTexEnv(0);
	C3D_TexEnv *env1 = C3D_GetTexEnv(1);
	C3D_TexEnvInit(env0);
	C3D_TexEnvInit(env1);
	/* RGB must not use the broken per-vertex lighting colour. Keep the authored
	 * material alpha in the TEV constant instead of routing it through a shader
	 * uniform/cache shared with the opaque reflection pass: Kuruma and Pony use
	 * different glass RGB/alpha values and otherwise become solid white/black. */
	C3D_TexEnvSrc(env0, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT);
	C3D_TexEnvFunc(env0, C3D_RGB, GPU_MODULATE);
	C3D_TexEnvSrc(env0, C3D_Alpha, GPU_TEXTURE0, GPU_CONSTANT);
	C3D_TexEnvFunc(env0, C3D_Alpha, GPU_MODULATE);

	/* Preserve any per-vertex alpha separately. PRIMARY_COLOR.a is deliberately
	 * supplied without material alpha below, so the material is applied once. */
	C3D_TexEnvSrc(env1, C3D_RGB, GPU_PREVIOUS);
	C3D_TexEnvFunc(env1, C3D_RGB, GPU_REPLACE);
	C3D_TexEnvSrc(env1, C3D_Alpha, GPU_PREVIOUS, GPU_PRIMARY_COLOR);
	C3D_TexEnvFunc(env1, C3D_Alpha, GPU_MODULATE);
}

static void
matfxTextureRender(InstanceDataHeader *header, InstanceData *inst, uint32 flags,
	Texture *texture)
{
	Material *m = inst->material;
	
	/* Vehicle MatFX atomics share the same broken per-vertex lighting path as
	 * skinned peds on 3DS.  Sample the base texture directly so lighting can't
	 * turn individual triangles into dark facets. */
	matfxTextureShader->use();
	C3D_TexEnvColor(C3D_GetTexEnv(0), packTevMaterialColor(m->color));
	protectVehicleTexture(texture);
	RGBA vertexColor = m->color;
	vertexColor.alpha = 0xFF;
	setMaterialColor(flags, vertexColor);
	setTexture(0, texture);
	/* Vehicle plates, decals and LCS damage atomics often keep material alpha at
	 * 255 and carry the cutout solely in the texture.  Without this term their
	 * transparent white background is drawn as an opaque rectangle. */
	rw::SetRenderState(VERTEXALPHA,
		inst->vertexAlpha || m->color.alpha != 0xFF || textureHasAlpha(texture));
	bool depthOffset = isVehicleDepthOffsetTexture(texture);
	if(depthOffset)
		C3D_DepthMap(true, -1.0f, 0.0030f);
	drawInst(header, inst);
	if(depthOffset)
		C3D_DepthMap(true, -1.0f, 0.0f);
}

void
matfxDefaultRender(InstanceDataHeader *header, InstanceData *inst, uint32 flags)
{
	matfxTextureRender(header, inst, flags, inst->material->texture);
}


static RawMatrix normal2texcoord = {
#ifdef RESTORIES_3DS_BUILD
	{ -0.5f,  0.0f, 0.0f }, 0.0f,
#else
	{ 0.5f,  0.0f, 0.0f }, 0.0f,
#endif
	{ 0.0f, -0.5f, 0.0f }, 0.0f,
	{ 0.0f,  0.0f, 1.0f }, 0.0f,
	{ 0.5f,  0.5f, 0.0f }, 1.0f
};

void
uploadEnvMatrix(Frame *frame)
{
	Matrix invMat;
	if(frame == nil)
		frame = engine->currentCamera->getFrame();

	// cache the matrix across multiple meshes
	static RawMatrix envMtx;
	RawMatrix invMtx;
	C3D_Mtx mtx;
	
	#ifdef RESTORIES_3DS_BUILD
	/* Match the Leeds vehicle mapping: reflection turns only with camera yaw.
	 * Removing pitch keeps the strip centred and the flipped U axis restores
	 * left/right correspondence on the 3DS camera texture. */
	Matrix tmp = *frame->getLTM();
	tmp.at.z = 0.0f;
	tmp.at = normalize(tmp.at);
	tmp.right.x = -tmp.at.y;
	tmp.right.y = tmp.at.x;
	tmp.right.z = 0.0f;
	tmp.up.set(0.0f, 0.0f, 1.0f);
	tmp.pos.set(0.0f, 0.0f, 0.0f);
	tmp.flags = Matrix::TYPEORTHONORMAL;
	Matrix::invert(&invMat, &tmp);
	#else
	/* GTA III/VC use the stock fixed environment-map frame and authored probe. */
	Matrix::invert(&invMat, frame->getLTM());
	#endif
	convMatrix(&invMtx, &invMat);
	invMtx.pos.set(0.0f, 0.0f, 0.0f);
	RawMatrix::mult(&envMtx, &invMtx, &normal2texcoord);

	#ifdef RESTORIES_3DS_BUILD
	RawMatrix *uploadMtx = &envMtx;
	#else
	/* Preserve the original GTA III/VC probe coordinates.  The old white-
	 * overbright fix changed only the TEV blend and coefficient, not mapping. */
	RawMatrix *uploadMtx = &invMtx;
	#endif
	mtx.r[0].x = uploadMtx->right.x;
	mtx.r[1].x = uploadMtx->right.y;
	mtx.r[2].x = uploadMtx->right.z;
	mtx.r[3].x = uploadMtx->rightw;
	mtx.r[0].y = uploadMtx->up.x;
	mtx.r[1].y = uploadMtx->up.y;
	mtx.r[2].y = uploadMtx->up.z;
	mtx.r[3].y = uploadMtx->upw;
	mtx.r[0].z = uploadMtx->at.x;
	mtx.r[1].z = uploadMtx->at.y;
	mtx.r[2].z = uploadMtx->at.z;
	mtx.r[3].z = uploadMtx->atw;
	mtx.r[0].w = uploadMtx->pos.x;
	mtx.r[1].w = uploadMtx->pos.y;
	mtx.r[2].w = uploadMtx->pos.z;
	mtx.r[3].w = uploadMtx->posw;
	
	c3dUniformMatrix4fv(U(u_texMatrix), 1, 0, &mtx);
}

void
matfxEnvRender(InstanceDataHeader *header, InstanceData *inst, uint32 flags,
	Texture *baseTexture, MatFX::Env *env, Atomic *atomic,
	RGBAf *ambient, bool *ambientValid, bool *envMatrixValid)
{
	Material *m;
	m = inst->material;
	/* Glass and translucent lamp covers already contain their intended dark
	 * tint and alpha in the base material.  Adding an opaque environment term
	 * makes them look like milky white plastic, so keep them on the verified
	 * texture/material-only path. */
	if(inst->vertexAlpha || m->color.alpha != 0xFF || textureHasAlpha(baseTexture)){
		matfxTextureRender(header, inst, flags, baseTexture);
		return;
	}

	if(env->coefficient == 0.0f
#ifndef RESTORIES_3DS_BUILD
	   || env->tex == nil
#endif
	){
		matfxTextureRender(header, inst, flags, baseTexture);
		return;
	}

#ifdef RESTORIES_3DS_BUILD
	bool playerReflection = atomic->clump == lcsPlayerVehicleClump;
	bool transitionReflection = atomic->clump == lcsTransitionVehicleClump;
	bool nearTrafficReflection = false;
	if(!playerReflection && !transitionReflection){
		const Matrix *vehicleMatrix = atomic->clump->getFrame()->getLTM();
		const Matrix *cameraMatrix = engine->currentCamera->getFrame()->getLTM();
		V3d delta;
		delta.x = vehicleMatrix->pos.x - cameraMatrix->pos.x;
		delta.y = vehicleMatrix->pos.y - cameraMatrix->pos.y;
		delta.z = vehicleMatrix->pos.z - cameraMatrix->pos.z;
		/* Past 35 metres even a high-detail traffic model occupies too few pixels
		 * for the extra sheen to read.  Leave those and all lower LODs on the base
		 * material path. */
		nearTrafficReflection =
			delta.x * delta.x + delta.y * delta.y + delta.z * delta.z <
			35.0f * 35.0f;
		if(!nearTrafficReflection){
			matfxTextureRender(header, inst, flags, baseTexture);
			return;
		}
	}
	Texture *reflectionTexture = playerReflection ? lcsPlayerVehicleReflection :
		(transitionReflection ? lcsTransitionVehicleReflection :
		 lcsTrafficVehicleReflection);
	Frame *reflectionFrame = playerReflection ? lcsPlayerVehicleReflectionFrame :
		(transitionReflection ? lcsTransitionVehicleReflectionFrame :
		 lcsTrafficVehicleReflectionFrame);
	if(reflectionTexture == nil || reflectionFrame == nil){
		matfxTextureRender(header, inst, flags, baseTexture);
		return;
	}
#endif

	envShader->use();
	C3D_TexEnvColor(C3D_GetTexEnv(0), packTevMaterialColor(m->color));
	float coefficient = env->coefficient;
	if(coefficient < 0.0f) coefficient = 0.0f;
	if(coefficient > 1.0f) coefficient = 1.0f;
	#ifdef RESTORIES_3DS_BUILD
	if(!*ambientValid){
		*ambient = getAtomicAmbientLight(atomic);
		*ambientValid = true;
	}
	if(playerReflection)
		coefficient *= lcsPlayerVehicleReflectionStrength;
	else if(transitionReflection)
		coefficient *= lcsTransitionVehicleReflectionStrength;
	C3D_TexEnvColor(C3D_GetTexEnv(4), packTevReflectionColor(coefficient, *ambient));
	#else
	C3D_TexEnvColor(C3D_GetTexEnv(1), packTevReflectionColor(coefficient, *ambient));
	#endif
	protectVehicleTexture(baseTexture);

	setTexture(0, baseTexture);
#ifdef RESTORIES_3DS_BUILD
	setTexture(1, reflectionTexture);
	if(!*envMatrixValid){
		uploadEnvMatrix(reflectionFrame);
		*envMatrixValid = true;
	}
#else
	/* Restore the original cheap MatFX highlight: one static authored texture,
	 * not an extra camera render or framebuffer capture. */
	setTexture(1, env->tex);
	if(!*envMatrixValid){
		uploadEnvMatrix(env->frame);
		*envMatrixValid = true;
	}
#endif

	setMaterialColor(flags, m->color);

	// float fxparams[2];
	// fxparams[0] = env->coefficient;
	// fxparams[1] = env->fbAlpha ? 0.0f : 1.0f;

	// c3dUniform2fv(U(u_fxparams), 1, fxparams);
	// static float zero[4];
	// static float one[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	// This clamps the vertex color below. With it we can achieve both PC and PS2 style matfx
	// if(MatFX::modulateEnvMap)
	// 	c3dUniform4fv(U(u_colorClamp), 1, zero);
	// else
	// 	c3dUniform4fv(U(u_colorClamp), 1, one);

	rw::SetRenderState(VERTEXALPHA,
		inst->vertexAlpha || m->color.alpha != 0xFF || textureHasAlpha(baseTexture));
	bool depthOffset = isVehicleDepthOffsetTexture(baseTexture);
	if(depthOffset)
		C3D_DepthMap(true, -1.0f, 0.0030f);
	drawInst(header, inst);
	if(depthOffset)
		C3D_DepthMap(true, -1.0f, 0.0f);
}

void
matfxRenderCB(Atomic *atomic, InstanceDataHeader *header)
{
	uint32 flags = atomic->geometry->flags;
	setWorldMatrix(atomic->getFrame()->getLTM());

	setAttribPointers(header);
	RGBAf ambient;
	bool ambientValid = false;
	bool envMatrixValid = false;

	InstanceData *inst = header->inst;
	int32 n = header->numMeshes;

	while(n--){
		MatFX *matfx = MatFX::get(inst->material);

		if(matfx == nil)
			matfxDefaultRender(header, inst, flags);
		else switch(matfx->type){
		case MatFX::ENVMAP:
			matfxEnvRender(header, inst, flags, inst->material->texture,
				&matfx->fx[0].env, atomic, &ambient, &ambientValid,
				&envMatrixValid);
			break;
		default:
			matfxDefaultRender(header, inst, flags);
			break;
		}
		inst++;
	}
}

ObjPipeline*
makeMatFXPipeline(void)
{
	ObjPipeline *pipe = ObjPipeline::create();
	pipe->instanceCB = defaultInstanceCB;
	pipe->uninstanceCB = defaultUninstanceCB;
	pipe->renderCB = matfxRenderCB;
	pipe->pluginID = ID_MATFX;
	pipe->pluginData = 0;
	return pipe;
}

static void*
matfxOpen(void *o, int32, int32)
{
	matFXGlobals.pipelines[PLATFORM_3DS] = makeMatFXPipeline();
	matfxTextureShader = Shader::create(VSH_PRG_MATFXBASE, combinerMatFXTextureOnly, false);
	assert(matfxTextureShader);
#ifdef RESTORIES_3DS_BUILD
	envShader = Shader::create(VSH_PRG_MATFX, combinerLCSPlayerReflection, false);
#else
	envShader = Shader::create(VSH_PRG_MATFX, combiner_matfx, false);
#endif
	assert(envShader);
	return o;
}

static void*
matfxClose(void *o, int32, int32)
{
	((ObjPipeline*)matFXGlobals.pipelines[PLATFORM_3DS])->destroy();
	matFXGlobals.pipelines[PLATFORM_3DS] = nil;
	matfxTextureShader->destroy();
	matfxTextureShader = nil;
#ifdef RESTORIES_3DS_BUILD
	lcsPlayerVehicleClump = nil;
	lcsPlayerVehicleReflection = nil;
	lcsPlayerVehicleReflectionFrame = nil;
	lcsPlayerVehicleReflectionStrength = 1.0f;
	lcsTrafficVehicleReflection = nil;
	lcsTrafficVehicleReflectionFrame = nil;
	lcsTransitionVehicleClump = nil;
	lcsTransitionVehicleReflection = nil;
	lcsTransitionVehicleReflectionFrame = nil;
	lcsTransitionVehicleReflectionStrength = 1.0f;
#endif
	envShader->destroy();
	envShader = nil;
	return o;
}

void
initMatFX(void)
{
	Driver::registerPlugin(PLATFORM_3DS, 0, ID_MATFX,
			       matfxOpen, matfxClose);
}

#else

void initMatFX(void) { }

#endif

}
}
