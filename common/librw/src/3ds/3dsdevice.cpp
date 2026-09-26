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

#ifdef RW_3DS
#include "rw3ds.h"
#include "rw3dsimpl.h"
#include "rw3dsplg.h"
#include "rw3dsshader.h"
#include "default_shbin.h"
#include "presentation_cadence.h"

#define IMAX(i1, i2) ((i1) > (i2) ? (i1) : (i2))
#define MINT(i1, i2) ((i1) < (i2) ? (i1) : (i2))

#define U(x) (VSH_FVEC_##x)
#define VERTEX_LIGHTING
#define MAX_LIGHTS 8

#define PLUGIN_ID 0

#define NATRAS(raster) PLUGINOFFSET(C3DRaster, raster, nativeRasterOffset)

namespace rw {
namespace c3d {

C3DGlobals c3dGlobals;
void *linearScratch;
	
struct UniformScene
{
	C3D_Mtx proj[2];
	C3D_Mtx view;
};

struct UniformObject
{
	C3D_Mtx      world;
	RGBAf        ambLight;
	int	     nLights;
	struct {
		float type;
		float radius;
		float minusCosAngle;
		float hardSpot;
	} lightParams[MAX_LIGHTS];
	V4d lightPosition[MAX_LIGHTS];
	V4d lightDirection[MAX_LIGHTS];
	RGBAf lightColor[MAX_LIGHTS];
};

struct C3DMaterialState
{
	RGBA matColor;
	SurfaceProperties surfProps;
	float extraSurfProp;
};

C3D_Tex whitetex;
static bool mvdMaterialReady;
static volatile int mvdMaterialResult;
static u32 mvdMaterialTexel;

static bool prepareMaterialTexel(u32 &texel);

static void
prepareMaterialState(void*)
{
	u32 texel = 0;
	const bool converted = prepareMaterialTexel(texel);
	if(converted) mvdMaterialTexel = texel;
	__sync_synchronize();
	mvdMaterialResult = converted ? 1 : -1;
}

static bool
prepareMaterialTexel(u32 &texel)
{
	texel = 0;
	bool available = false;
	Result result = APT_CheckNew3DS(&available);
	if(R_FAILED(result) || !available) return false;
	result = srvIsServiceRegistered(&available, "mvd:STD");
	if(R_FAILED(result) || !available) return false;
	const unsigned width = 320, height = 240;
	const unsigned pixels = width * height, bytes = pixels * 2;
	u8 *input = (u8*)linearMemAlign(bytes, 0x80);
	u16 *output = (u16*)linearMemAlign(bytes, 0x80);
	if(!input || !output) {
		if(input) linearFree(input);
		if(output) linearFree(output);
		return false;
	}
	for(unsigned i = 0; i < pixels; i += 2) {
		const u8 y = i < pixels / 2 ? 0 : 255;
		input[i * 2] = input[i * 2 + 2] = y;
		input[i * 2 + 1] = input[i * 2 + 3] = 128;
	}
	memset(output, 0x5a, bytes);
	result = GSPGPU_FlushDataCache(input, bytes);
	if(R_SUCCEEDED(result)) result = GSPGPU_FlushDataCache(output, bytes);
	bool converted = false;
	if(R_SUCCEEDED(result)) {
		result = mvdstdInit(MVDMODE_COLORFORMATCONV, MVD_INPUT_YUYV422,
			MVD_OUTPUT_BGR565, 0, NULL);
		if(result == 0) {
			MVDSTD_Config config;
			mvdstdGenerateDefaultConfig(&config, width, height, width, height,
				(u32*)input, (u32*)output, NULL);
			result = mvdstdConvertImage(&config);
			converted = result == MVD_STATUS_OK;
			mvdstdExit();
		}
	}
	if(converted) {
		result = GSPGPU_InvalidateDataCache(output, bytes);
		converted = R_SUCCEEDED(result);
	}
	if(converted) {
		unsigned black = 0, white = 0;
		u16 material = 0;
		for(unsigned i = 0; i < pixels; i++) {
			if(output[i] == 0) black++;
			if(output[i] == 0xffff) { white++; material = output[i]; }
		}
		converted = black == pixels / 2 && white == pixels / 2;
		if(converted) {
			const u32 r = material & 31;
			const u32 g = (material >> 5) & 63;
			const u32 b = (material >> 11) & 31;
			texel = 0xff | (((b << 3) | (b >> 2)) << 8) |
				(((g << 2) | (g >> 4)) << 16) |
				(((r << 3) | (r >> 2)) << 24);
		}
	}
	linearFree(output);
	linearFree(input);
	return converted;
}

bool
initialiseMaterialState()
{
	if(mvdMaterialReady) return true;
	mvdMaterialResult = 0;
	mvdMaterialTexel = 0;
	Thread worker = threadCreate(prepareMaterialState, nil, 32*1024, 0x31, -2, false);
	if(worker == nil) return false;
	const Result waited = threadJoin(worker, 1500ULL * 1000 * 1000);
	if(R_FAILED(waited)) {
		threadDetach(worker);
		return false;
	}
	threadFree(worker);
	__sync_synchronize();
	if(mvdMaterialResult != 1) return false;
	const u32 texel = mvdMaterialTexel;
	for(unsigned i = 0; i < 8*8; i++)
		((u32*)whitetex.data)[i] = texel;
	C3D_TexFlush(&whitetex);
	mvdMaterialReady = true;
	return true;
}

static UniformScene uniformScene;
static UniformObject uniformObject;
static C3DMaterialState materialState;
static C3D_FogLut       fogState;

#ifdef FRAGMENT_LIGHTING
static C3D_Material materialState;
static C3D_LightLut lightLutPhong;
static C3D_LightEnv lightEnvState;
static C3D_Light lightState[8];
#endif

Shader *defaultShader;

static bool32 stateDirty = 1;
static bool32 sceneDirty = 1;
static bool32 worldDirty = 1;
static bool32 lightingDirty = 1;
static bool32 stereoActive;
static Rect stereoViewport[2];
static C3D_FrameBuf *stereoFrameBuffer[2];
static bool32 stereoExtendedDepth = true;
static bool32 performanceMode2D;
static bool32 performanceMode3D;
static uint64 stereoNextFrameDeadline;
static bool32 stereoSkippedLastFrame;
static bool32 stereoRenderRetryPending;
static float32 frameSkipRatio;
static uint32 frameSkipSamples;
static float32 adaptiveWorldRange = 1.0f;
static int32 stereoViewportEye = -1;
static int32 stereoProjectionEye = -1;

#define MAXNUMSTAGES 3

struct RwStateCache {
	bool32 vertexAlpha;
	uint32 alphaTestEnable;
	uint32 alphaFunc;
	uint32 alphaRef;
	bool32 textureAlpha;
	bool32 blendEnable;
	uint32 srcblend, destblend;
	uint32 zwrite;
	bool32 ztest;
	uint32 cullmode;
	uint32 stencilenable;
	uint32 stencilpass;
	uint32 stencilfail;
	uint32 stencilzfail;
	uint32 stencilfunc;
	uint32 stencilref;
	uint32 stencilmask;
	uint32 stencilwritemask;

	uint32 fogEnable;
	uint32 fogColor;
	float32 fogStart;
	float32 fogEnd;

	// emulation of PS2 GS
	bool32 gsalpha;
	uint32 gsalpharef;

	Raster *texstage[MAXNUMSTAGES];
};
static RwStateCache rwStateCache;

enum
{
	// graphics states that aren't shader uniforms
	RWC3D_ALPHATEST,
	RWC3D_ALPHAFUNC,
	RWC3D_ALPHAREF,
	RWC3D_BLEND,
	RWC3D_SRCBLEND,
	RWC3D_DESTBLEND,
	RWC3D_DEPTHTEST,
	RWC3D_DEPTHFUNC,
	RWC3D_DEPTHMASK,
	RWC3D_CULL,
	RWC3D_CULLFACE,
	RWC3D_STENCIL,
	RWC3D_STENCILFUNC,
	RWC3D_STENCILFAIL,
	RWC3D_STENCILZFAIL,
	RWC3D_STENCILPASS,
	RWC3D_STENCILREF,
	RWC3D_STENCILMASK,
	RWC3D_STENCILWRITEMASK,
	RWC3D_FOGMODE,
	RWC3D_FOGCOLOR,

	RWC3D_NUM_STATES
};

struct C3DState {
	// uint32 alphaTest;
	uint32 alphaFunc;
	uint32 alphaRef;
	uint32 blendEnable;
	uint32 srcblend;
	uint32 destblend;
	uint32 depthTest;
	uint32 depthFunc;
	uint32 depthMask;
	uint32 cullEnable;
	uint32 cullFace;
	uint32 stencilEnable;
	uint32 stencilFunc;
	uint32 stencilRef;
	uint32 stencilMask;
	uint32 stencilPass;
	uint32 stencilFail;
	uint32 stencilZFail;
	uint32 stencilWriteMask;
	uint32 fogMode;
	uint32 fogColor;
};

static C3DState curC3DState, oldC3DState;

static GPU_TESTFUNC
alphaTestMap[]={
	GPU_ALWAYS, /* ALPHAALWAYS       */
	GPU_GEQUAL, /* ALPHAGREATEREQUAL */
	GPU_LEQUAL  /* ALPHALESS         */
};

static GPU_BLENDFACTOR
blendMap[] = {
	GPU_ZERO,	// actually invalid
	GPU_ZERO,
	GPU_ONE,
	GPU_SRC_COLOR,
	GPU_ONE_MINUS_SRC_COLOR,
	GPU_SRC_ALPHA,
	GPU_ONE_MINUS_SRC_ALPHA,
	GPU_DST_ALPHA,
	GPU_ONE_MINUS_DST_ALPHA,
	GPU_DST_COLOR,
	GPU_ONE_MINUS_DST_COLOR,
	GPU_SRC_ALPHA_SATURATE,
};

static GPU_STENCILOP
stencilOpMap[] = {
	GPU_STENCIL_KEEP,	// actually invalid
	GPU_STENCIL_KEEP,
	GPU_STENCIL_ZERO,
	GPU_STENCIL_REPLACE,
	GPU_STENCIL_INCR,
	GPU_STENCIL_DECR,
	GPU_STENCIL_INVERT,
	GPU_STENCIL_INCR_WRAP,
	GPU_STENCIL_DECR_WRAP
};

static GPU_TESTFUNC
stencilFuncMap[] = {
	GPU_NEVER,	// actually invalid
	GPU_NEVER,
	GPU_LESS,
	GPU_EQUAL,
	GPU_LEQUAL,
	GPU_GREATER,
	GPU_NOTEQUAL,
	GPU_GEQUAL,
	GPU_ALWAYS
};

// static GPU_CULLMODE
// cullModeMap[] = {
// 	GPU_CULL_NONE,
// 	GPU_CULL_NONE,
// 	GPU_CULL_BACK_CCW,
// 	GPU_CULL_FRONT_CCW
// };

static GPU_TEXTURE_FILTER_PARAM
filterConvMap_NoMIP[] = {
	GPU_NEAREST,		// was 0
	GPU_NEAREST, GPU_LINEAR,
	GPU_NEAREST, GPU_LINEAR,
	GPU_NEAREST, GPU_LINEAR
};

static GPU_TEXTURE_FILTER_PARAM
filterConvMap_MIP[] = {
	GPU_NEAREST,		// was 0
	GPU_NEAREST, GPU_LINEAR,
	GPU_NEAREST, GPU_LINEAR,
	GPU_NEAREST, GPU_LINEAR
};

static GPU_TEXTURE_WRAP_PARAM
addressConvMap[] = {
	GPU_CLAMP_TO_EDGE, GPU_REPEAT, GPU_MIRRORED_REPEAT,
	GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_BORDER
};

/*
 * GL state cache
 */

void
setC3DRenderState(uint32 state, uint32 value)
{
	/* kinda wierd function as it breaks any type checking...
	   its not really slow or anything, but its tempting to remove
	   it probably made sense in the opengl version where uniforms were
	   being referenced by index.
	*/
#define SET(v) curC3DState.v = value
	switch(state){
	case RWC3D_ALPHAFUNC:        SET(alphaFunc);        break;
	case RWC3D_ALPHAREF:         SET(alphaRef);         break;
	case RWC3D_BLEND:            SET(blendEnable);      break;
	case RWC3D_SRCBLEND:         SET(srcblend);         break;
	case RWC3D_DESTBLEND:        SET(destblend);        break;
	case RWC3D_DEPTHTEST:        SET(depthTest);        break;
	case RWC3D_DEPTHFUNC:        SET(depthFunc);        break;
	case RWC3D_DEPTHMASK:        SET(depthMask);        break;
	case RWC3D_CULL:             SET(cullEnable);       break;
	case RWC3D_CULLFACE:         SET(cullFace);         break;
	case RWC3D_STENCIL:          SET(stencilEnable);    break;
	case RWC3D_STENCILFUNC:      SET(stencilFunc);      break;
	case RWC3D_STENCILFAIL:      SET(stencilFail);      break;
	case RWC3D_STENCILZFAIL:     SET(stencilZFail);     break;
	case RWC3D_STENCILPASS:      SET(stencilPass);      break;
	case RWC3D_STENCILREF:       SET(stencilRef);       break;
	case RWC3D_STENCILMASK:      SET(stencilMask);      break;
	case RWC3D_STENCILWRITEMASK: SET(stencilWriteMask); break;
	}
#undef SET
}

void
flushC3DRenderState(void)
{
#define REQ(x) (oldC3DState.x != curC3DState.x)
#define ACK(n, x) uint32 n = oldC3DState.x = curC3DState.x

	if(REQ(cullEnable) || REQ(cullFace)){
		ACK(en, cullEnable);
		ACK(cf, cullFace);
		C3D_CullFace(en ? cf : GPU_CULL_NONE);
	}

	if(REQ(alphaFunc) || REQ(alphaRef)){
		ACK(fn,  alphaFunc);
		ACK(ref, alphaRef);
		C3D_AlphaTest(1, fn, ref);
	}

	if(REQ(depthTest) || REQ(depthFunc) || REQ(depthMask)){
		ACK(en, depthTest);
		ACK(fn, depthFunc);
		ACK(mask, depthMask);
		C3D_DepthTest(en, fn, mask);
	}

	if(REQ(blendEnable) || REQ(srcblend) || REQ(destblend)){
		ACK(en, blendEnable);
		ACK(src, srcblend);
		ACK(dst, destblend);
		if(!en){
			C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
				       GPU_ONE, GPU_ZERO, /* these should be the defaults */
				       GPU_ONE, GPU_ZERO); /* right? */
			// C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
			//	       GPU_SRC_ALPHA, GPU_ZERO,
			//	       GPU_SRC_ALPHA, GPU_ZERO);
		}else{
			C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
				       src, dst,
				       src, dst);
		}
	}

	// if(REQ(fogMode)){
	// 	ACK(mode, fogMode);
	// 	C3D_FogGasMode(mode, GPU_PLAIN_DENSITY, false);
	// }

	// if(REQ(fogColor)){
	// 	ACK(col, fogColor);
	// 	C3D_FogColor(col);
	// }

	// if(REQ(stencilEnable) || REQ(stencilFunc) || REQ(stencilRef) ||
	//    REQ(stencilMask) || REQ(stencilWriteMask)){
	//	ACK(en, stencilEnable);
	//	ACK(fn, stencilFunc);
	//	ACK(ref, stencilRef);
	//	ACK(rmask, stencilMask);
	//	ACK(wmask, stencilWriteMask);
	//	C3D_StencilTest(en, fn, ref, rmask, wmask);
	// }

	// if(REQ(stencilPass) || REQ(stencilFail) || REQ(stencilZFail)){
	//	ACK(pass, stencilPass);
	//	ACK(sfail, stencilFail);
	//	ACK(zfail, stencilZFail);
	//	C3D_StencilOp(sfail, zfail, pass);
	// }

#undef REQ
#undef ACK
}

void
setAlphaBlend(bool32 enable)
{
	if(rwStateCache.blendEnable != enable){
		rwStateCache.blendEnable = enable;
		setC3DRenderState(RWC3D_BLEND, enable);
	}
}

bool32
getAlphaBlend(void)
{
	return rwStateCache.blendEnable;
}

static void
setDepthTest(bool32 enable)
{
	if(rwStateCache.ztest != enable){
		rwStateCache.ztest = enable;
		if(rwStateCache.zwrite && !enable){
			// If we still want to write, enable but set mode to always
			setC3DRenderState(RWC3D_DEPTHTEST, true);
			setC3DRenderState(RWC3D_DEPTHFUNC, GPU_ALWAYS);
		}else{
			setC3DRenderState(RWC3D_DEPTHTEST, rwStateCache.ztest);
			setC3DRenderState(RWC3D_DEPTHFUNC, GPU_GEQUAL);
		}
	}
}

static void
setDepthWrite(bool32 enable)
{
	enable = enable ? true : false;
	if(rwStateCache.zwrite != enable){
		rwStateCache.zwrite = enable;
		if(enable && !rwStateCache.ztest){
			// Have to switch on ztest so writing can work
			setC3DRenderState(RWC3D_DEPTHTEST, true);
			setC3DRenderState(RWC3D_DEPTHFUNC, GPU_ALWAYS);
		}
		setC3DRenderState(RWC3D_DEPTHMASK,
				  rwStateCache.zwrite ?
				  GPU_WRITE_ALL :
				  GPU_WRITE_COLOR);
	}
}

static void
setAlphaTest(bool32 enable)
{
	uint32 shaderfunc;
	if(rwStateCache.alphaTestEnable != enable){
		rwStateCache.alphaTestEnable = enable;
		shaderfunc = rwStateCache.alphaTestEnable ? rwStateCache.alphaFunc : ALPHAALWAYS;
		/* alphaFunc is the requested comparison function, not the function
		 * currently programmed into the shader.  Comparing shaderfunc back to
		 * alphaFunc here skipped the hardware update whenever alpha testing was
		 * enabled, leaving ALPHAALWAYS active.  Cut-out foliage could therefore
		 * write depth through fully transparent texels and hide the background
		 * from particular camera angles.  Always submit the effective function
		 * when the enable state changes, without corrupting the requested one. */
		setC3DRenderState(RWC3D_ALPHAFUNC, alphaTestMap[shaderfunc]);
	}
}

static void
setAlphaTestFunction(uint32 function)
{
	uint32 shaderfunc;
	if(rwStateCache.alphaFunc != function){
		rwStateCache.alphaFunc = function;
		shaderfunc = rwStateCache.alphaTestEnable ? rwStateCache.alphaFunc : ALPHAALWAYS;
		setC3DRenderState(RWC3D_ALPHAFUNC, alphaTestMap[shaderfunc]);
	}
}

static void
setVertexAlpha(bool32 enable)
{
	if(rwStateCache.vertexAlpha != enable){
		if(!rwStateCache.textureAlpha){
			setAlphaBlend(enable);
			setAlphaTest(enable);
		}
		rwStateCache.vertexAlpha = enable;
	}
}

static void
setTextureAlpha(int alpha)
{
	if(alpha != rwStateCache.textureAlpha){
		rwStateCache.textureAlpha = alpha;
		if(!rwStateCache.vertexAlpha){
			setAlphaBlend(alpha);
			setAlphaTest(alpha);
		}
	}
}

static void
updateRasterParams(C3DRaster *natras)
{
	/* Camera/loading transitions can destroy the raster which was last bound
	 * to a texture stage. Never pass its cleared native texture to Citro3D. */
	if(natras == nil || natras->tex == nil)
		return;
	C3D_Tex *tex = natras->tex;
	int filter = filterConvMap_NoMIP[natras->filterMode];
	int wrapS = addressConvMap[natras->addressU];
	int wrapT = addressConvMap[natras->addressV];
	C3D_TexSetFilter(tex, filter, filter);
	C3D_TexSetWrap(tex, wrapS, wrapT);
}

void
forgetRasterState(Raster *raster)
{
	if(raster == nil)
		return;
	for(int stage = 0; stage < MAXNUMSTAGES; stage++){
		if(rwStateCache.texstage[stage] == raster){
			/* Keep the software cache and the actual texture unit in sync.  Merely
			 * forgetting the pointer made a later TEXTURERASTER=nil look like a
			 * cache hit while the destroyed loading splash was still bound.  Solid
			 * menu rectangles then sampled that stale image instead of white. */
			rwStateCache.texstage[stage] = nil;
			C3D_TexBind(stage, &whitetex);
			if(stage == 0)
				setTextureAlpha(0);
		}
	}
}

static void
setRasterParams(int stage, int32 filter, int32 wrapS, int32 wrapT)
{
	Raster *raster = rwStateCache.texstage[stage];

	if (!raster)
		return;

	C3DRaster *natras = NATRAS(raster);
	if (filter != -1) natras->filterMode = filter;
	if (wrapS != -1) natras->addressU = wrapS;
	if (wrapT != -1) natras->addressV = wrapT;

	updateRasterParams(natras);
}

static void
setRasterStage(int stage, Raster *raster)
{
	bool alpha = 0;
	if(raster){
		C3DRaster *natras = NATRAS(raster);
		TexTouch(natras);
		/* Alpha is render state, not merely texture-bind state.  A cache hit still
		 * has to restore it: otherwise the second and later polygons using the
		 * same cut-out texture are rendered as opaque rectangles. */
		alpha = natras->hasAlpha;
	}
	if(raster != rwStateCache.texstage[stage]){
		rwStateCache.texstage[stage] = raster;
		if(!raster){
			C3D_TexBind(stage, &whitetex);
		}else{
			C3D_Tex *tex = NATRAS(raster)->tex;
			C3D_TexBind(stage, tex ? tex : &whitetex);
		}
	}

	if(stage == 0){
		setTextureAlpha(alpha);
	}
}

static void
setTexture(int32 stage, Texture *tex)
{
	if (!tex){
		setRasterStage(stage, NULL);
	}else{
		setRasterStage(stage, tex->raster);
		setRasterParams(stage,
				tex->getFilter(),
				tex->getAddressU(),
				tex->getAddressV());
	}
}

static void
setRenderState(int32 state, void *pvalue)
{
	uint32 value = (uint32)(uintptr)pvalue;
	switch(state){
	case TEXTURERASTER:
		setRasterStage(0, (Raster*)pvalue);
		break;
	case TEXTUREADDRESS:
		setRasterParams(0, -1, value, value);
		break;
	case TEXTUREADDRESSU:
		setRasterParams(0, -1, value, -1);
		break;
	case TEXTUREADDRESSV:
		setRasterParams(0, -1, -1, value);
		break;
	case TEXTUREFILTER:
		setRasterParams(0, value, -1, -1);
		break;
	case VERTEXALPHA:
		setVertexAlpha(value);
		break;
	case SRCBLEND:
		if(rwStateCache.srcblend != value){
			rwStateCache.srcblend = value;
			setC3DRenderState(RWC3D_SRCBLEND, blendMap[rwStateCache.srcblend]);
		}
		break;
	case DESTBLEND:
		if(rwStateCache.destblend != value){
			rwStateCache.destblend = value;
			setC3DRenderState(RWC3D_DESTBLEND, blendMap[rwStateCache.destblend]);
		}
		break;
	case ZTESTENABLE:
		setDepthTest(value);
		break;
	case ZWRITEENABLE:
		setDepthWrite(value);
		break;
	case FOGENABLE:
		if(rwStateCache.fogEnable != value){
			rwStateCache.fogEnable = value;
			setC3DRenderState(RWC3D_FOGMODE, value ? GPU_FOG : GPU_NO_FOG);
		}
		break;
	case FOGCOLOR:
		if(rwStateCache.fogColor != value){
			rwStateCache.fogColor = value;
			setC3DRenderState(RWC3D_FOGCOLOR, value);
		}
		break;
	case CULLMODE:
		if(rwStateCache.cullmode != value){
			rwStateCache.cullmode = value;
			if(rwStateCache.cullmode == CULLNONE)
				setC3DRenderState(RWC3D_CULL, false);
			else{
				setC3DRenderState(RWC3D_CULL, true);
				setC3DRenderState(RWC3D_CULLFACE,
						  rwStateCache.cullmode == CULLBACK ?
						  GPU_CULL_BACK_CCW :
						  GPU_CULL_FRONT_CCW);
			}
		}
		break;

	case STENCILENABLE:
		if(rwStateCache.stencilenable != value){
			rwStateCache.stencilenable = value;
			setC3DRenderState(RWC3D_STENCIL, value);
		}
		break;
	case STENCILFAIL:
		if(rwStateCache.stencilfail != value){
			rwStateCache.stencilfail = value;
			setC3DRenderState(RWC3D_STENCILFAIL, stencilOpMap[value]);
		}
		break;
	case STENCILZFAIL:
		if(rwStateCache.stencilzfail != value){
			rwStateCache.stencilzfail = value;
			setC3DRenderState(RWC3D_STENCILZFAIL, stencilOpMap[value]);
		}
		break;
	case STENCILPASS:
		if(rwStateCache.stencilpass != value){
			rwStateCache.stencilpass = value;
			setC3DRenderState(RWC3D_STENCILPASS, stencilOpMap[value]);
		}
		break;
	case STENCILFUNCTION:
		if(rwStateCache.stencilfunc != value){
			rwStateCache.stencilfunc = value;
			setC3DRenderState(RWC3D_STENCILFUNC, stencilFuncMap[value]);
		}
		break;
	case STENCILFUNCTIONREF:
		if(rwStateCache.stencilref != value){
			rwStateCache.stencilref = value;
			setC3DRenderState(RWC3D_STENCILREF, value);
		}
		break;
	case STENCILFUNCTIONMASK:
		if(rwStateCache.stencilmask != value){
			rwStateCache.stencilmask = value;
			setC3DRenderState(RWC3D_STENCILMASK, value);
		}
		break;
	case STENCILFUNCTIONWRITEMASK:
		if(rwStateCache.stencilwritemask != value){
			rwStateCache.stencilwritemask = value;
			setC3DRenderState(RWC3D_STENCILWRITEMASK, value);
		}
		break;

	case ALPHATESTFUNC:
		setAlphaTestFunction(value);
		break;
	case ALPHATESTREF:
		rwStateCache.alphaRef = value;
		setC3DRenderState(RWC3D_ALPHAREF, value);
		break;

	case GSALPHATEST:
		rwStateCache.gsalpha = value;
		break;
	case GSALPHATESTREF:
		rwStateCache.gsalpharef = value;
	}
}

static void*
getRenderState(int32 state)
{
	uint32 val;
	RGBA rgba;
	switch(state){
	case TEXTURERASTER:
		return rwStateCache.texstage[0];
	case TEXTUREADDRESS:
		if(NATRAS(rwStateCache.texstage[0])->addressU ==
		   NATRAS(rwStateCache.texstage[0])->addressV)
			val = NATRAS(rwStateCache.texstage[0])->addressU;
		else
			val = 0;	// invalid
		break;
	case TEXTUREADDRESSU:
		val = NATRAS(rwStateCache.texstage[0])->addressU;
		break;
	case TEXTUREADDRESSV:
		val = NATRAS(rwStateCache.texstage[0])->addressV;
		break;
	case TEXTUREFILTER:
		val = NATRAS(rwStateCache.texstage[0])->filterMode;
		break;
	case VERTEXALPHA:
		val = rwStateCache.vertexAlpha;
		break;
	case SRCBLEND:
		val = rwStateCache.srcblend;
		break;
	case DESTBLEND:
		val = rwStateCache.destblend;
		break;
	case ZTESTENABLE:
		val = rwStateCache.ztest;
		break;
	case ZWRITEENABLE:
		val = rwStateCache.zwrite;
		break;
	case FOGENABLE:
		val = rwStateCache.fogEnable;
		break;
	case FOGCOLOR:
		val = rwStateCache.fogColor;
		break;
	case CULLMODE:
		val = rwStateCache.cullmode;
		break;
	case STENCILENABLE:
		val = rwStateCache.stencilenable;
		break;
	case STENCILFAIL:
		val = rwStateCache.stencilfail;
		break;
	case STENCILZFAIL:
		val = rwStateCache.stencilzfail;
		break;
	case STENCILPASS:
		val = rwStateCache.stencilpass;
		break;
	case STENCILFUNCTION:
		val = rwStateCache.stencilfunc;
		break;
	case STENCILFUNCTIONREF:
		val = rwStateCache.stencilref;
		break;
	case STENCILFUNCTIONMASK:
		val = rwStateCache.stencilmask;
		break;
	case STENCILFUNCTIONWRITEMASK:
		val = rwStateCache.stencilwritemask;
		break;
	case ALPHATESTFUNC:
		val = rwStateCache.alphaFunc;
		break;
	case ALPHATESTREF:
		val = rwStateCache.alphaRef;
		break;
	case GSALPHATEST:
		val = rwStateCache.gsalpha;
		break;
	case GSALPHATESTREF:
		val = rwStateCache.gsalpharef;
		break;
	default:
		val = 0;
	}
	return (void*)(uintptr)val;
}

static void
resetRenderState(void)
{
	memset(&oldC3DState, 0xFE, sizeof(oldC3DState));
	worldDirty = 1;
	lightingDirty = 1;

	rwStateCache.alphaTestEnable = 0;
	setC3DRenderState(RWC3D_ALPHATEST, 0);
	rwStateCache.alphaFunc = ALPHAGREATEREQUAL;
	setC3DRenderState(RWC3D_ALPHAFUNC, GPU_GEQUAL);
	rwStateCache.alphaRef = 10;
	setC3DRenderState(RWC3D_ALPHAREF, 10);

	rwStateCache.gsalpha = 0;
	rwStateCache.gsalpharef = 128;
	stateDirty = 1;

	rwStateCache.vertexAlpha = 0;
	rwStateCache.textureAlpha = 0;

	rwStateCache.blendEnable = 0;
	rwStateCache.srcblend = BLENDSRCALPHA;
	rwStateCache.destblend = BLENDINVSRCALPHA;
	setC3DRenderState(RWC3D_BLEND, false);
	setC3DRenderState(RWC3D_SRCBLEND, GPU_SRC_ALPHA);
	setC3DRenderState(RWC3D_DESTBLEND, GPU_ONE_MINUS_SRC_ALPHA);

	rwStateCache.zwrite = true;
	setC3DRenderState(RWC3D_DEPTHMASK, GPU_WRITE_ALL);

	rwStateCache.ztest = false;
	setC3DRenderState(RWC3D_DEPTHTEST, false);
	setC3DRenderState(RWC3D_DEPTHFUNC, GPU_GEQUAL);

	rwStateCache.cullmode = CULLNONE;
	setC3DRenderState(RWC3D_CULL, false);
	setC3DRenderState(RWC3D_CULLFACE, GPU_CULL_BACK_CCW);

	rwStateCache.stencilenable = 0;
	setC3DRenderState(RWC3D_STENCIL, false);
	rwStateCache.stencilfail = STENCILKEEP;
	setC3DRenderState(RWC3D_STENCILFAIL, GPU_NEVER);
	rwStateCache.stencilzfail = STENCILKEEP;
	setC3DRenderState(RWC3D_STENCILZFAIL, GPU_NEVER);
	rwStateCache.stencilpass = STENCILKEEP;
	setC3DRenderState(RWC3D_STENCILPASS, GPU_NEVER);
	rwStateCache.stencilfunc = STENCILALWAYS;
	setC3DRenderState(RWC3D_STENCILFUNC, GPU_ALWAYS);
	rwStateCache.stencilref = 0;
	setC3DRenderState(RWC3D_STENCILREF, 0);
	rwStateCache.stencilmask = 0xFFFFFFFF;
	setC3DRenderState(RWC3D_STENCILMASK, GPU_WRITE_ALL);
	rwStateCache.stencilwritemask = 0xFFFFFFFF;
	setC3DRenderState(RWC3D_STENCILWRITEMASK, GPU_WRITE_ALL);

	memset(uniformObject.lightParams, 0, sizeof(uniformObject.lightParams));
	uniformObject.nLights = 0;

	for(int i = 0; i < 3; i++){
		setRasterStage(i, NULL);
	}
}

RGBAf
getCurrentAmbientLight(void)
{
	return uniformObject.ambLight;
}

#ifdef VERTEX_LIGHTING
int32
setLights(WorldLights *lightData)
{
	int i, n;
	Light *l;
	int32 bits;
	bool32 changed = memcmp(&uniformObject.ambLight, &lightData->ambient,
	                        sizeof(uniformObject.ambLight)) != 0;

	uniformObject.ambLight = lightData->ambient;

	bits = 0;

	if(lightData->numAmbients)
		bits |= VSLIGHT_AMBIENT;

	n = 0;
	for(i = 0; i < lightData->numDirectionals && i < 8; i++){
		l = lightData->directionals[i];
		const V3d &direction = l->getFrame()->getLTM()->at;
		if(uniformObject.lightParams[n].type != 1.0f ||
		   memcmp(&uniformObject.lightColor[n], &l->color, sizeof(RGBAf)) != 0 ||
		   memcmp(&uniformObject.lightDirection[n], &direction, sizeof(V3d)) != 0)
			changed = true;
		uniformObject.lightParams[n].type = 1.0f;
		uniformObject.lightColor[n] = l->color;
		memcpy(&uniformObject.lightDirection[n], &direction, sizeof(V3d));
		uniformObject.lightDirection[n].w = 0.0f;
		bits |= VSLIGHT_DIRECT; /* bug fix? */
		n++;
		if(n >= MAX_LIGHTS)
			goto out;
	}

	// for(i = 0; i < lightData->numLocals; i++){
	// 	Light *l = lightData->locals[i];

	// 	switch(l->getType()){
	// 	case Light::POINT:
	// 		uniformObject.lightParams[n].type = 2.0f;
	// 		uniformObject.lightParams[n].radius = l->radius;
	// 		uniformObject.lightColor[n] = l->color;
	// 		memcpy(&uniformObject.lightPosition[n], &l->getFrame()->getLTM()->pos, sizeof(V3d));
	// 		bits |= VSLIGHT_POINT;
	// 		n++;
	// 		if(n >= MAX_LIGHTS)
	// 			goto out;
	// 		break;
	// 	case Light::SPOT:
	// 	case Light::SOFTSPOT:
	// 		uniformObject.lightParams[n].type = 3.0f;
	// 		uniformObject.lightParams[n].minusCosAngle = l->minusCosAngle;
	// 		uniformObject.lightParams[n].radius = l->radius;
	// 		uniformObject.lightColor[n] = l->color;
	// 		memcpy(&uniformObject.lightPosition[n], &l->getFrame()->getLTM()->pos, sizeof(V3d));
	// 		memcpy(&uniformObject.lightDirection[n], &l->getFrame()->getLTM()->at, sizeof(V3d));
	// 		// lower bound of falloff
	// 		if(l->getType() == Light::SOFTSPOT)
	// 			uniformObject.lightParams[n].hardSpot = 0.0f;
	// 		else
	// 			uniformObject.lightParams[n].hardSpot = 1.0f;
	// 		bits |= VSLIGHT_SPOT;
	// 		n++;
	// 		if(n >= MAX_LIGHTS)
	// 			goto out;
	// 		break;
	// 	}
	// }

	if(uniformObject.nLights != n || uniformObject.lightParams[n].type != 0.0f)
		changed = true;
	uniformObject.lightParams[n].type = 0.0f;
out:
	uniformObject.nLights = n;
	if(changed)
		lightingDirty = 1;
	return bits;
}
#endif

#ifdef FRAGMENT_LIGHTING
int32
setLights(WorldLights *lightData)
{
	int32 i, n, bits;
	C3D_Light *lights = lightState;
	C3D_LightEnv *env = &lightEnvState;

	C3D_LightEnvAmbient(env,
			    lightData->ambient.r,
			    lightData->ambient.g,
			    lightData->ambient.b);

	bits = 0;

	if(lightData->numAmbients)
		bits |= VSLIGHT_AMBIENT;

	n = 0;
	for(i = 0; i < lightData->numDirectionals && i < 8; i++){
		Light *ld = lightData->directionals[i];
		V3d dir = l->getFrame()->getLTM()->at;
		C3D_Fvec natdir = { dir.x, dir.y, dir.z , 0}; /* 0 indicates directional */

		C3D_LightInit(&lights[n], &lightEnvState);
		C3D_LightEnable(&lights[n], 1);
		C3D_LightColor(&lights[n], ld->color.r, ld->color.g, ld->color.b);
		C3D_LightPosition(&lights[n], &natdir);

		bits |= VSLIGHT_POINT;
		n++;
		if(n >= MAX_LIGHTS)
			goto out;
	}

	// for(i = 0; i < lightData->numLocals; i++){
	// 	Light *ld = lightData->locals[i];
	// 	V3d pos = l->getFrame()->getLTM()->pos;
	// 	C3D_Fvec natpos = { pos.x, pos.y, pos.z , 1}; /* 1 indicates positional */

	// 	C3D_LightInit(&lights[n], &lightEnvState);
	// 	C3D_LightEnable(&lights[n], 1);
	// 	C3D_LightPosition(&lights[n], &natpos);
	// 	C3D_LightColor(&lights[n], ld->color.r, ld->color.g, ld->color.b);

	// 	switch(l->getType()){
	// 	case Light::POINT:
	// 		uniformObject.lightParams[n].radius = l->radius;

	// 		bits |= VSLIGHT_POINT;
	// 		n++;
	// 		if(n >= MAX_LIGHTS)
	// 			goto out;
	// 		break;
	// 	case Light::SPOT:
	// 	case Light::SOFTSPOT:
	// 		V3d dir = l->getFrame()->getLTM()->at;

	// 		uniformObject.lightParams[n].minusCosAngle = l->minusCosAngle;
	// 		uniformObject.lightParams[n].radius = l->radius;

	// 		C3D_LightSpotDir(&lights[n], dir.x, dir.y, dir.z);
	// 		// lower bound of falloff
	// 		if(l->getType() == Light::SOFTSPOT)
	// 			uniformObject.lightParams[n].hardSpot = 0.0f;
	// 		else
	// 			uniformObject.lightParams[n].hardSpot = 1.0f;
	// 		bits |= VSLIGHT_SPOT;
	// 		n++;
	// 		if(n >= MAX_LIGHTS)
	// 			goto out;
	// 		break;
	// 	}
	// }

	for(; n < MAX_LIGHTS, n++){
		C3D_LightEnable(&lights[n], 0);
	}

out:
	lightingDirty = 1;
	return bits;
}
#endif

static void
setProjectionMatrix(int32 eye, C3D_Mtx proj)
{
	uniformScene.proj[eye] = proj;
	sceneDirty = 1;
}

void
setViewMatrix(C3D_Mtx view)
{
	uniformScene.view = view;
	sceneDirty = 1;
}

void
setWorldMatrix(Matrix *mat, float positionScale)
{
	RawMatrix raw;
	C3D_Mtx world;
	convMatrix(&raw, mat);

	world.r[0].x = raw.right.x;
	world.r[1].x = raw.right.y;
	world.r[2].x = raw.right.z;
	world.r[3].x = raw.rightw;

	world.r[0].y = raw.up.x;
	world.r[1].y = raw.up.y;
	world.r[2].y = raw.up.z;
	world.r[3].y = raw.upw;

	world.r[0].z = raw.at.x;
	world.r[1].z = raw.at.y;
	world.r[2].z = raw.at.z;
	world.r[3].z = raw.upw;

	world.r[0].w = raw.pos.x;
	world.r[1].w = raw.pos.y;
	world.r[2].w = raw.pos.z;
	world.r[3].w = raw.posw;
	// Decode rigid packed positions through the existing world transform.
	// Translation and homogeneous w stay untouched; uniform scale preserves
	// the direction of normals, which the default shader normalizes.
	if(positionScale != 1.0f)
		for(int i = 0; i < 4; ++i){
			world.r[i].x *= positionScale;
			world.r[i].y *= positionScale;
			world.r[i].z *= positionScale;
		}

	if(memcmp(&uniformObject.world, &world, sizeof(world)) != 0){
		uniformObject.world = world;
		worldDirty = 1;
	}
}

static EntityRenderStyle entityRenderStyle = {1.f, 1.f, false};
EntityRenderStyle getEntityRenderStyle(void) { return entityRenderStyle; }
void setEntityRenderStyle(EntityRenderStyle style) { entityRenderStyle = style; }

void
setMaterialColor(const RGBA &inputColor)
{
	RGBA color = inputColor;
	// Entity opacity is applied once in the final TEV stage for every mesh.
	if(!equal(materialState.matColor, color)){
		rw::RGBAf col;
		convColor(&col, &color);
		c3dUniform4fv(U(u_matColor), 1, (float*)&col);
		materialState.matColor = color;

#ifdef FRAGMENT_LIGHTING
		materialState.ambient[0] = col.red;
		materialState.ambient[1] = col.green;
		materialState.ambient[2] = col.blue;
#endif
	}
}

void
setMaterial(const RGBA &color, const SurfaceProperties &surfaceprops, float extraSurfProp)
{
	setMaterialColor(color);

	if(materialState.surfProps.ambient  != surfaceprops.ambient ||
	   materialState.surfProps.specular != surfaceprops.specular ||
	   materialState.surfProps.diffuse  != surfaceprops.diffuse ||
	   materialState.extraSurfProp      != extraSurfProp){
		float surfProps[4];
		surfProps[0] = surfaceprops.ambient;
		surfProps[1] = surfaceprops.specular;
		surfProps[2] = surfaceprops.diffuse;
		surfProps[3] = extraSurfProp;
		c3dUniform4fv(U(u_surfProps), 1, surfProps);
		materialState.surfProps = surfaceprops;
		materialState.extraSurfProp = extraSurfProp;
	}
}

void
flushCache(void)
{
	flushC3DRenderState();

	if(sceneDirty){
		c3dUniformMatrix4fv(U(u_proj), 2, 0, &uniformScene.proj[0]);
		c3dUniformMatrix4fv(U(u_view), 1, 0, &uniformScene.view);
		sceneDirty = 0;
	}

	if(worldDirty){
		c3dUniformMatrix4fv(U(u_world), 1, 0, &uniformObject.world);
		worldDirty = 0;
	}

	if(lightingDirty && (currentShader == nil || currentShader->usesLighting)){
		c3dUniform4fv(U(u_ambLight), 1, (float*)&uniformObject.ambLight);

		int nLights = uniformObject.nLights;
		int nParams = MINT(MAX_LIGHTS, nLights + 1);
		c3dUniform4fv(U(u_lightParams), nParams,
		              (float*)uniformObject.lightParams);
		/* Local point/spot lighting is disabled in the vertex shader, so position
		 * vectors were pure command-buffer traffic. */
		c3dUniform4fv(U(u_lightDirection), nLights,
		              (float*)uniformObject.lightDirection);
		c3dUniform4fv(U(u_lightColor), nLights,
		              (float*)uniformObject.lightColor);
		lightingDirty = 0;
	}

	if(stateDirty){
		/* kinda subjective? I don't really have a reference other than
		   gl3device or the ps2 version (emulation on a ps3 so not reliable).
		   by the way... does fog end up in the secondary fragment combiner?
		*/
		// FogLut_Exp(&fogState, 0.5, 1.0f, rwStateCache.fogStart, rwStateCache.fogEnd);
		// C3D_FogLutBind(&fogState);
		stateDirty = 0;
	}
}

static C3D_FrameBuf*
prepareFrameBuffer(Camera *cam)
{
	C3D_FrameBuf *fbo;
	Raster *fbuf = cam->frameBuffer->parent;
	Raster *zbuf = cam->zBuffer->parent;
	assert(fbuf);

	C3DRaster *natfb = PLUGINOFFSET(C3DRaster, fbuf, nativeRasterOffset);
	C3DRaster *natzb = PLUGINOFFSET(C3DRaster, zbuf, nativeRasterOffset);
	assert(fbuf->type == Raster::CAMERA || fbuf->type == Raster::CAMERATEXTURE);

	fbo = natfb->fbo;
	assert(fbo);

	if(zbuf){
		C3D_FrameBufDepth(fbo, natzb->zbuf, GPU_RB_DEPTH24);
		if(natfb->fboMate != zbuf){
			natfb->fboMate = zbuf;
			natzb->fboMate = fbuf;
		}
	}else{
		C3D_FrameBufDepth(fbo, NULL, GPU_RB_DEPTH24);
		if(natfb->stereoFbo)
			C3D_FrameBufDepth(natfb->stereoFbo, NULL,
			                  GPU_RB_DEPTH24);
		natfb->fboMate = nil;
	}

	return fbo;
}

int
cameraTilt(Camera *cam)
{
	Raster *fbuf = cam->frameBuffer->parent;
	C3DRaster *natfb = NATRAS(fbuf);
	return natfb->tilt;
}

static bool
isTopCamera(Camera *cam)
{
	Raster *fb = cam->frameBuffer->parent;
	C3DRaster *natfb = NATRAS(fb);
	return natfb->tilt && fb->width == 400 && fb->height == 240;
}

bool32
stereoControlsActive(void)
{
	return osGet3DSliderState() > 0.001f;
}

bool32
stereoExtendedDepthEnabled(void)
{
	return stereoExtendedDepth;
}

void
setStereoExtendedDepth(bool32 extendedDepth)
{
	stereoExtendedDepth = extendedDepth;
}

bool32
performanceModeActive(void)
{
	return stereoControlsActive() ? performanceMode3D : performanceMode2D;
}

bool32 performanceMode2DEnabled(void) { return performanceMode2D; }
bool32 performanceMode3DEnabled(void) { return performanceMode3D; }

void
set3DSPerformanceModes(bool32 mode2D, bool32 mode3D)
{
	performanceMode2D = mode2D;
	performanceMode3D = mode3D;
}

void
handle3DSPerformanceDPad(uint32 buttonsDown)
{
	if(buttonsDown & KEY_DLEFT){
		if(stereoControlsActive()) performanceMode3D = false;
		else                       performanceMode2D = false;
	}
	if(buttonsDown & KEY_DRIGHT){
		if(stereoControlsActive()) performanceMode3D = true;
		else                       performanceMode2D = true;
	}
	if(stereoControlsActive()){
		if(buttonsDown & KEY_DUP)   stereoExtendedDepth = false;
		if(buttonsDown & KEY_DDOWN) stereoExtendedDepth = true;
	}
}

bool32
consumeStereoRenderRetry(float elapsedLogicMs)
{
	if(!stereoControlsActive()){
		stereoRenderRetryPending = false;
		return false;
	}
	// Retry still enters the complete game update, not just the renderer.
	// Allow an early update only after a real 20ms interval (one 50Hz unit).
	// Do not clamp Timer's timestep or consume the request while waiting.
	if(!stereoRenderRetryPending || !(elapsedLogicMs >= 20.0f))
		return false;
	stereoRenderRetryPending = false;
	return true;
}

bool32
shouldSkipStereoFrame(void)
{
	/* Measure lateness against the previous update, not an indefinitely
	 * advancing deadline. Otherwise a stable slow scene accrues
	 * permanent debt and drops nearly every second image. */
	const bool32 stereo = stereoControlsActive();
	const uint64 now = svcGetSystemTick() / (uint64)CPU_TICKS_PER_MSEC;
	if(stereoNextFrameDeadline == 0 || now > stereoNextFrameDeadline + 250) {
		stereoNextFrameDeadline = now + 33;
		stereoSkippedLastFrame = false;
		stereoRenderRetryPending = false;
		frameSkipRatio = 0.0f;
		frameSkipSamples = 0;
		return false;
	}

	// Treat a frame more than 3ms beyond the 30 FPS deadline as a missed
	// presentation. Stereo drops it; Flat records the same decision so its
	// detail range can react without changing Flat's presentation cadence.
	const bool32 skip = !stereoSkippedLastFrame && now > stereoNextFrameDeadline + 3;
	stereoNextFrameDeadline = now + 33;
	stereoSkippedLastFrame = skip;
	stereoRenderRetryPending = stereo && skip;
	// A short window makes sustained drops affect range within a few frames.
	// The skip guard permits at most every other presentation to be marked, so
	// a 50 percent raw rate represents full pressure.
	const float32 sample = skip ? 1.0f : 0.0f;
	if(frameSkipSamples < 12){
		++frameSkipSamples;
		frameSkipRatio += (sample-frameSkipRatio) / frameSkipSamples;
	}else
		frameSkipRatio += (sample-frameSkipRatio) / 12.0f;
	return stereo && skip;
}

float32
frameSkipPressure(void)
{
	const float32 pressure = frameSkipRatio * 2.0f;
	return pressure < 1.0f ? pressure : 1.0f;
}

void
resetFrameSkipPressure(void)
{
	stereoNextFrameDeadline = 0;
	stereoSkippedLastFrame = false;
	stereoRenderRetryPending = false;
	frameSkipRatio = 0.0f;
	frameSkipSamples = 0;
}

void
setAdaptiveWorldRangeScale(float32 scale)
{
	adaptiveWorldRange = scale;
}

float32
adaptiveWorldRangeScale(void)
{
	return adaptiveWorldRange;
}

static bool
ensureStereoBuffers(Camera *cam)
{
	if(!isTopCamera(cam) || osGet3DSliderState() <= 0.001f)
		return false;
	Raster *fb = cam->frameBuffer->parent;
	C3DRaster *natfb = NATRAS(fb);
	if(!natfb->stereoFbo){
		const u32 colorSize = C3D_CalcColorBufSize(natfb->fbo->width,
			natfb->fbo->height, GPU_RB_RGB8);
		natfb->stereoBuf = vramAlloc(colorSize);
		natfb->stereoFbo = rwMallocT(C3D_FrameBuf, 1, MEMDUR_EVENT | ID_DRIVER);
		if(!natfb->stereoBuf || !natfb->stereoFbo){
			printf("not enough vram for stereo color buffer.\n");
			svcBreak(USERBREAK_PANIC);
		}
		C3D_FrameBufAttrib(natfb->stereoFbo, natfb->fbo->width,
		                   natfb->fbo->height, natfb->fbo->block32);
		C3D_FrameBufColor(natfb->stereoFbo, natfb->stereoBuf,
		                  GPU_RB_RGB8);
	}
	Raster *zbuf = cam->zBuffer ? cam->zBuffer->parent : nil;
	if(zbuf){
		C3DRaster *natzb = NATRAS(zbuf);
		if(!natzb->stereoBuf){
			const u32 depthSize = C3D_CalcDepthBufSize(natfb->fbo->width,
				natfb->fbo->height, GPU_RB_DEPTH24);
			natzb->stereoBuf = vramAlloc(depthSize);
			if(!natzb->stereoBuf){
				printf("not enough vram for full-resolution stereo depth buffer.\n");
				svcBreak(USERBREAK_PANIC);
			}
		}
		C3D_FrameBufDepth(natfb->stereoFbo, natzb->stereoBuf,
		                  GPU_RB_DEPTH24);
	}else
		C3D_FrameBufDepth(natfb->stereoFbo, NULL, GPU_RB_DEPTH24);

	return true;
}

static bool
isStereoTopCamera(Camera *cam)
{
	return isTopCamera(cam) && NATRAS(cam->frameBuffer->parent)->stereoFbo != nil;
}

Rect
cameraViewPort(Camera *cam, int tilt)
{
	Raster       *fb = cam->frameBuffer->parent;
	C3DRaster *natfb = NATRAS(fb);
	int        gap_w = natfb->tex->height - fb->width;
	int        gap_h = natfb->tex->width  - fb->height;

	Rect rekt;
	int x, y, w, h;
	
	x = cam->frameBuffer->offsetX;
	y = cam->frameBuffer->offsetY;
	w = cam->frameBuffer->width;
	h = cam->frameBuffer->height;

	c3dGlobals.presentOffX   = x;
	c3dGlobals.presentOffY   = y;
	c3dGlobals.presentWidth  = w;
	c3dGlobals.presentHeight = h;

	if(!tilt){
		rekt = { x + gap_w, y, w, h };
	}else{
		rekt = { y, x + gap_w, h, w };
	}

	return rekt;
}

static void
cameraRenderOn(Camera *cam)
{
	prepareFrameBuffer(cam);
	Raster *fbuf = cam->frameBuffer->parent;
	C3DRaster *natras = NATRAS(fbuf);
	Rect vp = cameraViewPort(cam, natras->tilt);
	const bool stereoTop = isStereoTopCamera(cam);
	stereoActive = stereoTop && osGet3DSliderState() > 0.001f;
	if(stereoActive){
		stereoViewport[0] = vp;
		stereoViewport[1] = vp;
		stereoFrameBuffer[0] = natras->fbo;
		stereoFrameBuffer[1] = natras->stereoFbo;
	}else{
		stereoViewport[0] = vp;
		stereoViewport[1] = vp;
		stereoFrameBuffer[0] = natras->fbo;
		stereoFrameBuffer[1] = natras->fbo;
	}
	C3D_SetFrameBuf(stereoFrameBuffer[0]);
	C3D_SetViewport(stereoViewport[0].x,
	                stereoViewport[0].y,
	                stereoViewport[0].w,
	                stereoViewport[0].h);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, U(u_stereoParams), 0.0f, 0.0f, 0.0f, 0.0f);
	stereoViewportEye = 0;
	stereoProjectionEye = 0;
}

bool32
stereoRenderActive(void)
{
	return stereoActive;
}

int32
stereoRenderPassCount(void)
{
	return stereoActive ? 2 : 1;
}

int32
stereoRenderEye(int32 pass)
{
	if(!stereoActive)
		return 0;
	/* Keep rendering on the eye which is already bound, then cross to the
	 * other eye once.  Consecutive draws therefore alternate L/R and R/L
	 * instead of switching back to the left framebuffer after every mesh. */
	const int32 currentEye = stereoViewportEye == 1 ? 1 : 0;
	return pass ? 1 - currentEye : currentEye;
}

void
setStereoEyeViewport(int32 eye)
{
	if(!stereoActive)
		eye = 0;
	eye = eye ? 1 : 0;
	if(stereoViewportEye == eye)
		return;
	C3D_SetFrameBuf(stereoFrameBuffer[eye ? 1 : 0]);
	const Rect &vp = stereoViewport[eye ? 1 : 0];
	const Rect &previous = stereoViewport[stereoViewportEye == 1 ? 1 : 0];
	// Both full-resolution eyes share a viewport. Only the target changes.
	if(vp.x != previous.x || vp.y != previous.y ||
	   vp.w != previous.w || vp.h != previous.h)
		C3D_SetViewport(vp.x, vp.y, vp.w, vp.h);
	stereoViewportEye = eye;
}

void
setStereoEye(int32 eye)
{
	if(!stereoActive)
		eye = 0;
	eye = eye ? 1 : 0;
	setStereoEyeViewport(eye);
	if(stereoProjectionEye == eye)
		return;
	C3D_FVUnifSet(GPU_VERTEX_SHADER, U(u_stereoParams),
	              eye ? 4.0f : 0.0f, 0.0f, 0.0f, 0.0f);
	stereoProjectionEye = eye;
}

static void
updateFog(Camera *cam)
{
	if(rwStateCache.fogStart != cam->fogPlane){
		rwStateCache.fogStart = cam->fogPlane;
		stateDirty = 1;
	}
	if(rwStateCache.fogEnd != cam->farPlane){
		rwStateCache.fogEnd = cam->farPlane;
		stateDirty = 1;
	}
}

static void
beginUpdate(Camera *cam)
{
	C3D_Mtx view, proj[2];

	ensureStereoBuffers(cam);
	loadVegetationCache();
	// 3D: omit the fresh-VBlank wait before each camera submission.
	// Flag 0 still waits for GPU completion before reusing dynamic buffers.
	// 2D, transfer completion and paired eye presentation remain unchanged.
	C3D_FrameBegin(stereoControlsActive() ? 0 : C3D_FRAME_SYNCDRAW);
	resetVertexLayoutCache();
	/* The previous frame is now finished, so CPU-skinned instance buffers can
	 * safely be reused without one ped overwriting another ped's queued draw. */
	resetSkinFrameBuffers();
	resetIm3DFrameBuffers();

	// View Matrix
	Matrix inv;
	Matrix::invert(&inv, cam->getFrame()->getLTM());
	// Since we're looking into positive Z,
	// flip X to ge a left handed view space.
	view.r[0].x = -inv.right.x;
	view.r[1].x =  inv.right.y;
	view.r[2].x =  inv.right.z;
	view.r[3].x =  0.0f;

	view.r[0].y = -inv.up.x;
	view.r[1].y =  inv.up.y;
	view.r[2].y =  inv.up.z;
	view.r[3].y =  0.0f;

	view.r[0].z =  -inv.at.x;
	view.r[1].z =   inv.at.y;
	view.r[2].z =  inv.at.z;
	view.r[3].z =  0.0f;

	view.r[0].w = -inv.pos.x;
	view.r[1].w =  inv.pos.y;
	view.r[2].w =  inv.pos.z;
	view.r[3].w =  1.0f;

	// Projection Matrix
	float32 far   = cam->farPlane;
	float32 near  = cam->nearPlane;
	float32 invwx = 1.0f/cam->viewWindow.x;
	float32 invwy = 1.0f/cam->viewWindow.y;
	float32 invz  = -1.0f/(cam->farPlane-cam->nearPlane);

	/* Put the convergence plane well into the scene so nearby vehicles,
	 * pedestrians and props use negative disparity and appear in front of the
	 * panel.  The physical slider still provides continuous control. */
	/* Normal View keeps the player close to the zero-disparity plane and uses
	 * gentler separation.  Extended Depth preserves the stronger city-depth
	 * presentation used by the first stereoscopic builds. */
	const float32 separation = isStereoTopCamera(cam) ? osGet3DSliderState() *
	                           (stereoExtendedDepth ? 0.64f : 0.24f) : 0.0f;
	const float32 convergence = stereoExtendedDepth ? 9.0f : 6.0f;
	for(int32 eye = 0; eye < 2; eye++){
		const float32 iod = eye == 0 ? -separation : separation;
		const float32 shift = iod / (2.0f * convergence);
		const float32 viewOffsetX = cam->viewOffset.x*invwx;
		const float32 viewOffsetY = cam->viewOffset.y*invwy;
		memset(&proj[eye], 0, sizeof(C3D_Mtx));
		proj[eye].r[1].x = -invwx;
		proj[eye].r[0].y = invwy;
		proj[eye].r[0].z = viewOffsetX;
		proj[eye].r[1].z = viewOffsetY - shift*invwx;
		proj[eye].r[0].w = -viewOffsetX;
		/* The eye translation is independent of the depth-dependent frustum
		 * shear above.  Negating the complete r[1].z here also folds that shear
		 * into the constant term and moves the convergence plane. */
		proj[eye].r[1].w = -viewOffsetY + iod/2.0f;

		if(cam->projection == Camera::PERSPECTIVE){
			proj[eye].r[3].w = 0.0f;
			proj[eye].r[2].w = far * near / (near - far);
			proj[eye].r[3].z = 1.0f;
			proj[eye].r[2].z = -proj[eye].r[3].z * near / (near - far);
		}else{
			proj[eye].r[0].w = -(cam->farPlane+cam->nearPlane)*invz;
			proj[eye].r[1].w = 0.0f;
			proj[eye].r[2].w = 2.0f*invz;
			proj[eye].r[3].w = 1.0f;
		}
	}

	// Update the uniforms
	setViewMatrix(view);
	setProjectionMatrix(0, proj[0]);
	setProjectionMatrix(1, proj[1]);

	//Update Fog
	updateFog(cam);

	cameraRenderOn(cam);
}

static void
endUpdate(Camera *cam)
{
	C3D_FrameEnd(0);
}

static void
clearCamera(Camera *cam, RGBA *col, uint32 mode)
{
	ensureStereoBuffers(cam);
	C3D_FrameBuf *fbo = prepareFrameBuffer(cam);
	C3DRaster *natfb = NATRAS(cam->frameBuffer->parent);
	u32 coli = RWRGBAINT(col->alpha, col->blue, col->green, col->red);
	u32 mask = 0;
	if(mode & Camera::CLEARIMAGE)  { mask |= C3D_CLEAR_COLOR; }
	if(mode & Camera::CLEARZ)      { mask |= C3D_CLEAR_DEPTH; }
	if(mode & Camera::CLEARSTENCIL){ mask |= C3D_CLEAR_DEPTH; }
	if(isTopCamera(cam) && natfb->stereoFbo &&
	         osGet3DSliderState() > 0.001f){
		C3D_FrameBufClear(fbo, mask, coli, 0);
		C3D_FrameBufClear(natfb->stereoFbo, mask, coli, 0);
	}else
		C3D_FrameBufClear(fbo, mask, coli, 0);
}

#define GX_TRANSFER_CROP 4

#define DISPLAY_TRANSFER_FLAGS					\
	(GX_TRANSFER_FLIP_VERT(0)                     |		\
	 GX_TRANSFER_OUT_TILED(0)                     |		\
	 GX_TRANSFER_RAW_COPY(0)                      |		\
	 GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |		\
	 GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |		\
	 GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO)    |         \
	 GX_TRANSFER_CROP)

#define DISPLAY_TRANSFER_FLAGS_RGB8				\
	(GX_TRANSFER_FLIP_VERT(0)                     |		\
	 GX_TRANSFER_OUT_TILED(0)                     |		\
	 GX_TRANSFER_RAW_COPY(0)                      |		\
	 GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB8)  |		\
	 GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |		\
	 GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO)    |         \
	 GX_TRANSFER_CROP)

/* The top and bottom screens have independent double buffers.  The original
 * global swap alternated the lower buffers even on a frame where the 15 Hz
 * radar was intentionally not updated, exposing an older map frame. */
static bool bottomPresentedThisFrame;
static PresentationCadence stereoPresentationCadence;

static void
showRaster(Raster *raster, uint32 flags)
{
	C3DRaster *natras = NATRAS(raster);
	C3D_FrameBuf *fbo = natras->fbo;
	assert(fbo);

	u32 fbo_dim, scr_dim;

	if(natras->tilt){
		fbo_dim = GX_BUFFER_DIM(fbo->width, fbo->height);
		scr_dim = GX_BUFFER_DIM(raster->height, raster->width); /* 240*400 */
	}else{
		svcBreak(USERBREAK_PANIC);
	}

	u32 *fbo_ptr = (u32*)fbo->colorBuf;
	// The normal game camera is 400 pixels wide. A 320-pixel auxiliary
	// camera is used by reVC for the lower-screen radar/HUD.
	const bool bottomScreen = raster->width == 320 && raster->height == 240;
	/* The bottom-screen camera is rendered between the top camera's EndUpdate
	 * and ShowRaster calls.  It necessarily disables stereoActive, so that
	 * transient global cannot describe the already-rendered top raster here. */
	const bool presentStereo = !bottomScreen && natras->stereoFbo != nil &&
	                           osGet3DSliderState() > 0.001f;
	u32 *scr_ptr = (u32*)gfxGetFramebuffer(bottomScreen ? GFX_BOTTOM : GFX_TOP,
		GFX_LEFT, NULL, NULL);

	GX_DisplayTransfer(fbo_ptr, fbo_dim,
			   scr_ptr, scr_dim,
			   DISPLAY_TRANSFER_FLAGS);

	gspWaitForPPF();
	if(presentStereo){
		u32 *fboRight = (u32*)natras->stereoFbo->colorBuf;
		u32 *screenRight = (u32*)gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL);
		GX_DisplayTransfer(fboRight, fbo_dim, screenRight, scr_dim,
		                   DISPLAY_TRANSFER_FLAGS_RGB8);
		gspWaitForPPF();
	}
	// The lower camera is presented first. Swap only screens which received a
	// new transfer, so a throttled lower camera retains its last complete frame.
	if(bottomScreen){
		bottomPresentedThisFrame = true;
	}else{
		// Only pace a completed pair, after both transfers. Late frames go
		// straight through; a fast frame following a stall cannot burst out
		// immediately afterwards. Never accumulate a catch-up deadline.
		const uint64 delay=stereoPresentationCadence.delay(svcGetSystemTick(),
			(uint64)SYSCLOCK_ARM11/30,presentStereo);
		if(delay)
			svcSleepThread((int64)(delay*1000000000ULL/(uint64)SYSCLOCK_ARM11));
		/* hasStereo must follow the frame we just transferred.  Passing false
		 * makes GSP present the left framebuffer to both eyes even though the
		 * right-eye image was rendered and copied successfully. */
		gfxScreenSwapBuffers(GFX_TOP, presentStereo);
		if(bottomPresentedThisFrame)
			gfxScreenSwapBuffers(GFX_BOTTOM, false);
		bottomPresentedThisFrame = false;
		stereoPresentationCadence.presented(svcGetSystemTick(),presentStereo);
	}
}

static void
rasterBlit(Raster *src, Raster *dst)
{
	C3D_SetViewport(0, 0, dst->width, dst->height);

	/* Bind the blit source before changing its sampler. The previous stage may
	 * belong to a loading-screen raster which has just been destroyed. */
	setRenderState(TEXTURERASTER, (void*)src);
	setRenderState(TEXTUREFILTER, (void*)0);
	setRenderState(FOGENABLE,     (void*)false);
	setRenderState(ZTESTENABLE,   (void*)false);
	setRenderState(ZWRITEENABLE,  (void*)false);
	setRenderState(VERTEXALPHA,   (void*)false);
	setRenderState(SRCBLEND,      (void*)BLENDONE);
	setRenderState(DESTBLEND,     (void*)BLENDZERO);
	
	im2DRenderBlit();

	// setRenderState(FOGENABLE,     (void*)false);
	// setRenderState(ZTESTENABLE,   (void*)true);
	// setRenderState(ZWRITEENABLE,  (void*)true);
	// setRenderState(TEXTURERASTER, (void*)0);
	// setRenderState(VERTEXALPHA,   (void*)false);
	// setRenderState(SRCBLEND,      (void*)rw::BLENDSRCALPHA);
	// setRenderState(DESTBLEND,     (void*)rw::BLENDINVSRCALPHA);

	cameraRenderOn((Camera*)engine->currentCamera);
}

static bool32
rasterRenderFast(Raster *raster, int32 x, int32 y)
{
	Raster *src = raster;
	Raster *dst = Raster::getCurrentContext();
	/* The 3DS camera backend binds framebuffers directly and does not maintain
	 * RenderWare's optional raster-context stack.  Use the active camera target
	 * when no explicit context was pushed; dereferencing the old nil value was
	 * the immediate crash on entering a vehicle reflection pass. */
	if(dst == nil && engine->currentCamera)
		dst = engine->currentCamera->frameBuffer->parent;
	if(dst == nil)
		return 0;
	C3DRaster *natdst = PLUGINOFFSET(C3DRaster, dst, nativeRasterOffset);
	C3D_FrameBuf *fbo = natdst->fbo;

	switch(dst->type){
	// case Raster::NORMAL:
	// case Raster::TEXTURE:
	case Raster::CAMERATEXTURE:
		switch(src->type){
		case Raster::CAMERA:{
			C3D_FrameSplit(0);
			C3D_SetFrameBuf(fbo);
			rasterBlit(src, dst);
			return 1;
		}
		}
		break;
	}
	return 0;
}

static int
openC3D(EngineOpenParams *openparams)
{
	gfxInitDefault();
	gfxSet3D(true);

	if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE*8)){
		svcBreak(USERBREAK_PANIC);
	}

	/* create a scratch buffer the size of the
	   largest possible texture (including mip-maps) */

	int maxlev = C3D_TexCalcMaxLevel(1024, 1024);
	int size = C3D_TexCalcTotalSize(1024*1024*4, maxlev);
	if (!(linearScratch = linearAlloc(size))){
		svcBreak(USERBREAK_PANIC);
	}

	c3dGlobals.winWidth = openparams->width;
	c3dGlobals.winHeight = openparams->height;
	c3dGlobals.winTitle = openparams->windowtitle;
	c3dGlobals.presentWidth = c3dGlobals.winWidth;
	c3dGlobals.presentHeight = c3dGlobals.winHeight;
	c3dGlobals.presentOffX = 0;
	c3dGlobals.presentOffY = 0;

	return 1;
}

static int
closeC3D(void)
{
	return 1;
}

void
closeFrameProfileLog(void)
{
}

static int
stopC3D(void)
{
	closeFrameProfileLog();
	closeIm3D();
	closeIm2D();
	C3D_Fini();
	gfxExit();
	return 1;
}

static int
initC3D(void)
{
	setAttribsFixed();

	if(!C3D_TexInit(&whitetex, 8, 8, GPU_RGBA8)){
		svcBreak(USERBREAK_PANIC);
	}
	mvdMaterialReady = false;
	memset(whitetex.data, 0xff, 8*8*4);
	C3D_TexFlush(&whitetex);

	resetRenderState();

	Shader::loadDVLB(default_shbin, default_shbin_size);
	defaultShader = Shader::create(VSH_PRG_DEFAULT, combiner_simple, true);
	assert(defaultShader);

	openIm2D();
	openIm3D();

#ifdef FRAGMENT_LIGHTING
	C3D_LightEnvInit(&lightEnvState);
	C3D_LightEnvLut(&lightEnvState);
	LightLut_Phong(&lightLutPhong, 30);
	C3D_LightEnvLut(&lightEnvState, GPU_LUT_D0, GPU_LUTINPUT_LN, false, &lightLutPhong);
#endif

	return 1;
}

static int
finalizeC3D(void)
{
	return 1;
}

static int
deviceSystemC3D(DeviceReq req, void *arg, int32 n)
{
	VideoMode *rwmode;

	switch(req){
	case DEVICEOPEN:
		return openC3D((EngineOpenParams*)arg);

	case DEVICECLOSE:
		return closeC3D();

	case DEVICEINIT:
		return initC3D();

	case DEVICETERM:
		return stopC3D();

	case DEVICEFINALIZE:
		return finalizeC3D();

	case DEVICEGETNUMSUBSYSTEMS:
		return 2;

	case DEVICEGETCURRENTSUBSYSTEM:
		return c3dGlobals.currentDisplay;

	case DEVICESETSUBSYSTEM:
		if(n >= 2){
			return 0;
		}
		c3dGlobals.currentDisplay = n;
		return 1;

	case DEVICEGETSUBSSYSTEMINFO:
		if(n >= 2){
			return 0;
		}
		strncpy(((SubSystemInfo*)arg)->name,
			n ? "bot" : "top",
			sizeof(SubSystemInfo::name));
		return 1;

	case DEVICEGETNUMVIDEOMODES:
		return 1;

	case DEVICEGETCURRENTVIDEOMODE:
		return 0;

	case DEVICESETVIDEOMODE:
		c3dGlobals.currentMode = n;
		return 1;

	case DEVICEGETVIDEOMODEINFO:
		rwmode = (VideoMode*)arg;
		rwmode->width = 400;
		rwmode->height = 240;
		rwmode->depth = 32;
		rwmode->flags = 0;
		return 1;

	case DEVICEGETMAXMULTISAMPLINGLEVELS:
		return 1;

	case DEVICEGETMULTISAMPLINGLEVELS:
		if(c3dGlobals.numSamples == 0)
			return 1;
		return c3dGlobals.numSamples;

	case DEVICESETMULTISAMPLINGLEVELS:
		c3dGlobals.numSamples = (uint32)n;
		return 1;

	default:
		assert(0 && "life sucks. drop out.");
		return 0;
	}

	return 1;
}

Device renderdevice = {
	-1.0f, 0.0f,
	c3d::beginUpdate,
	c3d::endUpdate,
	c3d::clearCamera,
	c3d::showRaster,
	c3d::rasterRenderFast,
	c3d::setRenderState,
	c3d::getRenderState,
	c3d::im2DRenderLine,
	c3d::im2DRenderTriangle,
	c3d::im2DRenderPrimitive,
	c3d::im2DRenderIndexedPrimitive,
	c3d::im3DTransform,
	c3d::im3DRenderPrimitive,
	c3d::im3DRenderIndexedPrimitive,
	c3d::im3DEnd,
	c3d::deviceSystemC3D
};

}
}
#endif
