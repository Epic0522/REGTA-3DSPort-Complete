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
#include "rw3dsshader.h"
#include "default_shbin.h"

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
static bool32 objectDirty = 1;

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

	if(REQ(fogMode)){
		ACK(mode, fogMode);
		C3D_FogGasMode(mode, GPU_PLAIN_DENSITY, false);
	}

	if(REQ(fogColor)){
		ACK(col, fogColor);
		C3D_FogColor(col);
	}

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
		if(rwStateCache.alphaFunc != shaderfunc){
			rwStateCache.alphaFunc = shaderfunc;
			setC3DRenderState(RWC3D_ALPHAFUNC, alphaTestMap[shaderfunc]);
		}
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
	C3D_Tex *tex = natras->tex;
	int filter = filterConvMap_NoMIP[natras->filterMode];
	int wrapS = addressConvMap[natras->addressU];
	int wrapT = addressConvMap[natras->addressV];
	C3D_TexSetFilter(tex, filter, filter);
	C3D_TexSetWrap(tex, wrapS, wrapT);
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
	if(raster != rwStateCache.texstage[stage]){
		rwStateCache.texstage[stage] = raster;
		if(!raster){
			C3D_TexBind(stage, &whitetex);
		}else{
			C3D_Tex *tex = NATRAS(raster)->tex;
			alpha = NATRAS(raster)->hasAlpha;
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

int
rasterStageCamera()
{
	Raster *ras = rwStateCache.texstage[0];
	return (!ras) ? 0 : ras->type == Raster::CAMERATEXTURE;
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

int32
setLights(WorldLights *lightData)
{
	int i, n;
	Light *l;
	int32 bits;

	uniformObject.ambLight = lightData->ambient;

	bits = 0;

	if(lightData->numAmbients)
		bits |= VSLIGHT_AMBIENT;

	n = 0;
	for(i = 0; i < lightData->numDirectionals && i < 8; i++){
		l = lightData->directionals[i];
		uniformObject.lightParams[n].type = 1.0f;
		uniformObject.lightColor[n] = l->color;
		memcpy(&uniformObject.lightDirection[n], &l->getFrame()->getLTM()->at, sizeof(V3d));
		bits |= VSLIGHT_POINT;
		n++;
		if(n >= MAX_LIGHTS)
			goto out;
	}

	for(i = 0; i < lightData->numLocals; i++){
		Light *l = lightData->locals[i];

		switch(l->getType()){
		case Light::POINT:
			uniformObject.lightParams[n].type = 2.0f;
			uniformObject.lightParams[n].radius = l->radius;
			uniformObject.lightColor[n] = l->color;
			memcpy(&uniformObject.lightPosition[n], &l->getFrame()->getLTM()->pos, sizeof(V3d));
			bits |= VSLIGHT_POINT;
			n++;
			if(n >= MAX_LIGHTS)
				goto out;
			break;
		case Light::SPOT:
		case Light::SOFTSPOT:
			uniformObject.lightParams[n].type = 3.0f;
			uniformObject.lightParams[n].minusCosAngle = l->minusCosAngle;
			uniformObject.lightParams[n].radius = l->radius;
			uniformObject.lightColor[n] = l->color;
			memcpy(&uniformObject.lightPosition[n], &l->getFrame()->getLTM()->pos, sizeof(V3d));
			memcpy(&uniformObject.lightDirection[n], &l->getFrame()->getLTM()->at, sizeof(V3d));
			// lower bound of falloff
			if(l->getType() == Light::SOFTSPOT)
				uniformObject.lightParams[n].hardSpot = 0.0f;
			else
				uniformObject.lightParams[n].hardSpot = 1.0f;
			bits |= VSLIGHT_SPOT;
			n++;
			if(n >= MAX_LIGHTS)
				goto out;
			break;
		}
	}

	uniformObject.lightParams[n].type = 0.0f;
out:
	uniformObject.nLights = n;
	objectDirty = 1;
	return bits;
}

void
setViewMatrix(C3D_Mtx view)
{
	uniformScene.view = view;
	sceneDirty = 1;
}

void
setWorldMatrix(Matrix *mat)
{
	RawMatrix raw;
	convMatrix(&raw, mat);

	uniformObject.world.r[0].x = raw.right.x;
	uniformObject.world.r[1].x = raw.right.y;
	uniformObject.world.r[2].x = raw.right.z;
	uniformObject.world.r[3].x = raw.rightw;

	uniformObject.world.r[0].y = raw.up.x;
	uniformObject.world.r[1].y = raw.up.y;
	uniformObject.world.r[2].y = raw.up.z;
	uniformObject.world.r[3].y = raw.upw;

	uniformObject.world.r[0].z = raw.at.x;
	uniformObject.world.r[1].z = raw.at.y;
	uniformObject.world.r[2].z = raw.at.z;
	uniformObject.world.r[3].z = raw.upw;

	uniformObject.world.r[0].w = raw.pos.x;
	uniformObject.world.r[1].w = raw.pos.y;
	uniformObject.world.r[2].w = raw.pos.z;
	uniformObject.world.r[3].w = raw.posw;

	objectDirty = 1;
}

void
setMaterial(const RGBA &color, const SurfaceProperties &surfaceprops, float extraSurfProp)
{
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

	if(materialState.surfProps.ambient != surfaceprops.ambient ||
	   materialState.surfProps.specular != surfaceprops.specular ||
	   materialState.surfProps.diffuse != surfaceprops.diffuse ||
	   materialState.extraSurfProp != extraSurfProp){
		float surfProps[4];
		surfProps[0] = surfaceprops.ambient;
		surfProps[1] = surfaceprops.specular;
		surfProps[2] = surfaceprops.diffuse;
		surfProps[3] = extraSurfProp;
		c3dUniform4fv(U(u_surfProps), 1, surfProps);
		materialState.surfProps = surfaceprops;
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

	if(objectDirty){
		c3dUniformMatrix4fv(U(u_world), 1, 0, &uniformObject.world);
#ifdef VERTEX_LIGHTING
		int nLights = uniformObject.nLights;
		int nParams = (nLights < MAX_LIGHTS) ? (nLights + 1) : MAX_LIGHTS;
		c3dUniform4fv(U(u_ambLight), 1, (float*)&uniformObject.ambLight);
		c3dUniform4fv(U(u_lightParams),    nParams, (float*)uniformObject.lightParams);
		c3dUniform4fv(U(u_lightPosition),  nLights, (float*)uniformObject.lightPosition);
		c3dUniform4fv(U(u_lightDirection), nLights, (float*)uniformObject.lightDirection);
		c3dUniform4fv(U(u_lightColor),     nLights, (float*)uniformObject.lightColor);
#endif
		objectDirty = 0;
	}

	if(stateDirty){
		/* kinda subjective? I don't really have a reference other than
		   gl3device or the ps2 version (emulation on a ps3 so not reliable). */
		FogLut_Exp(&fogState, 0.5, 1.0f, rwStateCache.fogStart, rwStateCache.fogEnd);
		C3D_FogLutBind(&fogState);
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
		C3D_FrameBufDepth(fbo, natzb->zbuf, GPU_RB_DEPTH24_STENCIL8);
		if(natfb->fboMate != zbuf){
			natfb->fboMate = zbuf;
			natzb->fboMate = fbuf;
		}
	}else{
		C3D_FrameBufDepth(fbo, NULL, GPU_RB_DEPTH24_STENCIL8);
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

Rect
cameraViewPort(Camera *cam, int tilt, int side)
{
	Raster       *fb = cam->frameBuffer->parent;
	C3DRaster *natfb = NATRAS(fb);
	int        gap_w = natfb->tex->height - fb->width;
	int        gap_h = natfb->tex->width  - fb->height;

	int x, y, w, h;
	Rect rekt;

	x = cam->frameBuffer->offsetX + side;
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

static Rect vpL, vpR;

static void
cameraRenderOn(Camera *cam)
{
	prepareFrameBuffer(cam);
	
	Raster      *fbuf = cam->frameBuffer->parent;
	C3DRaster *natras = NATRAS(fbuf);
	int             s = natras->tex->height / 2;

	vpL = cameraViewPort(cam, natras->tilt, 0);
	vpR = cameraViewPort(cam, natras->tilt, s);

	C3D_SetFrameBuf(natras->fbo);
}

static void
cameraLHS()
{
	C3D_FVUnifSet(GPU_VERTEX_SHADER, U(u_stereoParams), 0, 0, 0, 0);
	C3D_SetViewport(vpL.x, vpL.y, vpL.w, vpL.h);
}

static void
cameraRHS()
{
	C3D_FVUnifSet(GPU_VERTEX_SHADER, U(u_stereoParams), 4, 0, 0, 0);
	C3D_SetViewport(vpR.x, vpR.y, vpR.w, vpR.h);
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

void
setProjectionMatrix(int side, Camera *cam, int tilt, float iod)
{
	C3D_Mtx *proj = &uniformScene.proj[side];
	
	float32 far   = cam->farPlane;
	float32 near  = cam->nearPlane;
	float32 invwx = 1.0f/cam->viewWindow.x;
	float32 invwy = 1.0f/cam->viewWindow.y;
	float32 invz  = -1.0f/(cam->farPlane-cam->nearPlane);

	float32 screen = 2.0f;
	float32  shift = iod / (2.0f*screen);

	proj->r[0].x = 0.0f;
	proj->r[1].x = 0.0f;
	proj->r[2].x = 0.0f;
	proj->r[3].x = 0.0f;

	proj->r[0].y = 0.0f;
	proj->r[1].y = 0.0f;
	proj->r[2].y = 0.0f;
	proj->r[3].y = 0.0f;

	if (tilt){
		proj->r[1].x =-invwx;
		proj->r[0].y = invwy;
	}else {
		proj->r[1].x =-invwx;
		proj->r[0].y = invwy;
	}

	proj->r[0].z = cam->viewOffset.x*invwx;
	proj->r[1].z = cam->viewOffset.y*invwy;
	proj->r[0].w = -proj->r[0].z;
	proj->r[1].w = -proj->r[1].z + (iod / 2.0f);

	if(cam->projection == Camera::PERSPECTIVE){
		proj->r[3].w = 0.0f;
		proj->r[2].w = far * near / (near - far);
		proj->r[3].z = 1.0f;
		proj->r[2].z = -proj->r[3].z * shift * near / (near - far);
	}else{
		proj->r[0].w = -(cam->farPlane+cam->nearPlane)*invz;
		proj->r[1].w = 0.0f;
		proj->r[2].w = 2.0f*invz;
		proj->r[3].w = 1.0f;
	}
}

static void
beginUpdate(Camera *cam)
{
	C3D_Mtx view;
	int tilt = cameraTilt(cam);

	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

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

	// Update the uniforms
	float iod = osGet3DSliderState() / 4.0f;
	setViewMatrix(view);
	setProjectionMatrix(0, cam, tilt, -iod);
	setProjectionMatrix(1, cam, tilt, +iod),

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
	C3D_FrameBuf *fbo = prepareFrameBuffer(cam);
	u32 coli = RWRGBAINT(col->alpha, col->blue, col->green, col->red);
	u32 mask = 0;
	if(mode & Camera::CLEARIMAGE)  { mask |= C3D_CLEAR_COLOR; }
	if(mode & Camera::CLEARZ)      { mask |= C3D_CLEAR_DEPTH; }
	if(mode & Camera::CLEARSTENCIL){ mask |= C3D_CLEAR_DEPTH; }
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

static void
showRaster(Raster *raster, uint32 flags)
{
	C3DRaster *natras = NATRAS(raster);
	C3D_FrameBuf *fbo = natras->fbo;

	assert(fbo);

	u32 fbo_dim, scr_dim;

	if(natras->tilt){
		fbo_dim = GX_BUFFER_DIM(fbo->width, fbo->height / 2); /* 256x400 */
		scr_dim = GX_BUFFER_DIM(raster->height, raster->width); /* 240*400 */
	}else{
		svcBreak(USERBREAK_PANIC);
	}

	u32 *fbo_lhs = (u32*)fbo->colorBuf;
	u32 *fbo_rhs = (u32*)fbo->colorBuf + (fbo->height / 2) * fbo->width;
	u32 *scr_lhs = (u32*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT,  NULL, NULL);
	u32 *scr_rhs = (u32*)gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL);

	GX_DisplayTransfer(fbo_lhs, fbo_dim, scr_lhs, scr_dim, DISPLAY_TRANSFER_FLAGS);
	gspWaitForPPF();
	GX_DisplayTransfer(fbo_rhs, fbo_dim, scr_rhs, scr_dim, DISPLAY_TRANSFER_FLAGS);
	gspWaitForPPF();
	gfxSwapBuffers();
}

static void
rasterBlit(Raster *src, Raster *dst)
{
	/* for some reason the PICA200 doesn't like big viewports...
	   makes sense I guess. 
	   Big framebuffer = ok. 
	   Big VP(>512) = no.
	   Either that or I'm just retarded and haven't actually written a test program to cross
	   reference with documentation like everyone else in this community.
	*/
	
	int w = dst->width;
	int h = dst->height;
	int i;

	setRenderState(TEXTUREFILTER, (void*)0);
	setRenderState(FOGENABLE,     (void*)false);
	setRenderState(ZTESTENABLE,   (void*)false);
	setRenderState(ZWRITEENABLE,  (void*)false);
	setRenderState(TEXTURERASTER, (void*)src);
	setRenderState(VERTEXALPHA,   (void*)false);
	setRenderState(SRCBLEND,      (void*)BLENDONE);
	setRenderState(DESTBLEND,     (void*)BLENDZERO);
	
	for(i = 0; i < 2; i++){
		C3D_SetViewport(w * i, 0, w, h);
		im2DRenderBlit(i);
	}
	
	setRenderState(FOGENABLE,     (void*)false);
	setRenderState(ZTESTENABLE,   (void*)true);
	setRenderState(ZWRITEENABLE,  (void*)true);
	setRenderState(TEXTURERASTER, (void*)0);
	setRenderState(VERTEXALPHA,   (void*)false);
	setRenderState(SRCBLEND,      (void*)rw::BLENDSRCALPHA);
	setRenderState(DESTBLEND,     (void*)rw::BLENDINVSRCALPHA);
	
	cameraRenderOn((Camera*)engine->currentCamera);
}

static bool32
rasterRenderFast(Raster *raster, int32 x, int32 y)
{
	Raster       *src = raster;
	Raster       *dst = Raster::getCurrentContext();
	C3DRaster *natdst = PLUGINOFFSET(C3DRaster, dst, nativeRasterOffset);
	C3D_FrameBuf *fbo = natdst->fbo;

	switch(dst->type){
	case Raster::NORMAL:
	case Raster::TEXTURE:
	case Raster::CAMERATEXTURE:
		switch(src->type){
		case Raster::CAMERA:{
			assert(fbo);
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

static int
stopC3D(void)
{
	closeIm3D();
	closeIm2D();
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
	memset(whitetex.data, 0xff, 8*8*4);

	resetRenderState();

	Shader::loadDVLB(default_shbin, default_shbin_size);
	defaultShader = Shader::create(VSH_PRG_DEFAULT, combiner_simple);
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
