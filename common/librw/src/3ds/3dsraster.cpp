#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../rwbase.h"
#include "../rwerror.h"
#include "../rwplg.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwengine.h"
#include "rw3ds.h"
#include "rw3dsimpl.h"
#include "tex/swizzle.h"
#include "memory.h"

#define PLUGIN_ID ID_DRIVER

#define IMAX(i1, i2) ((i1) > (i2) ? (i1) : (i2))
#define MINT(i1, i2) ((i1) < (i2) ? (i1) : (i2))

void memoryInfo();

namespace rw {
namespace c3d {

int32 nativeRasterOffset;

int gForceCompression = 1;
int gForceMipmaps = 0;
  
static uint32
getLevelSize(Raster *raster, int32 level)
{
	int s = raster->originalStride;
	int h = raster->originalHeight;
	return C3D_TexCalcLevelSize(s * h, level);
}

static uint8_t*
getImagePtr(C3D_Tex* tex, int level)
{
	return C3D_Tex2DGetImagePtr(tex, level, NULL);
}

static bool
hasIntermediateAlpha(const C3DRaster *natras, const uint8 *data, uint32 size)
{
	if(!natras->hasAlpha || data == nil)
		return false;

	bool intermediate = false;
	bool transparent = false;
	if(natras->format == GPU_ETC1A4){
		/* Each ETC1A4 block stores eight alpha bytes followed by eight colour
		 * bytes.  Every nibble is one pixel's alpha. */
		for(uint32 block = 0; block + 16 <= size; block += 16)
			for(uint32 i = 0; i < 8; ++i){
				const uint8 packed = data[block + i];
				const uint8 lo = packed & 0xF;
				const uint8 hi = packed >> 4;
				transparent |= lo == 0 || hi == 0;
				intermediate |= (lo != 0 && lo != 0xF) ||
				                (hi != 0 && hi != 0xF);
			}
	}else if(natras->format == GPU_RGBA8){
		/* The upload path stores this format in ABGR byte order. */
		for(uint32 i = 0; i + 3 < size; i += 4){
			transparent |= data[i] == 0;
			intermediate |= data[i] != 0 && data[i] != 0xFF;
		}
	}
	/* A cut-out with anti-aliased edges contains zero-alpha texels.  Keeping
	 * those on the cut-out path avoids restoring a second draw for foliage,
	 * fences and decals.  Glass textures use a translucent floor instead. */
	return intermediate && !transparent;
}
  
  
#define GX_TRANSFER_FMT_NONE ((GX_TRANSFER_FORMAT)0xff)

static void
textureFormatMapping(Raster *raster, C3DRaster *natras)
{
	switch(raster->format & 0xF00){
#define FMT(_SRC, _FMT_TEX, _ALPHA, _BPP, _DEPTH, _FMT_XFER) \
	case _SRC:					     \
		natras->format   = _FMT_TEX;		     \
		natras->hasAlpha = _ALPHA;		     \
		natras->bpp      = _BPP;		     \
		raster->depth    = _DEPTH;		     \
		natras->transfer = _FMT_XFER;		     \
		break
	case Raster::DEFAULT:
		FMT(Raster::C8888,   GPU_RGBA8,    1, 4, 32, GX_TRANSFER_FMT_RGBA8);
		FMT(Raster::C888,    GPU_RGB8,     0, 3, 24, GX_TRANSFER_FMT_RGB8);
		FMT(Raster::C1555,   GPU_RGBA5551, 1, 2, 16, GX_TRANSFER_FMT_RGB5A1);
		FMT(Raster::C4444,   GPU_RGBA4,    1, 2, 16, GX_TRANSFER_FMT_RGBA4);
		FMT(Raster::C565,    GPU_RGB565,   0, 2, 16, GX_TRANSFER_FMT_RGB565);
		FMT(Raster::LUM8,    GPU_L8,       0, 1,  8, GX_TRANSFER_FMT_NONE);
#undef FMT
	default:
		printf("unknown texture format %lx", raster->format & 0xF00);
		svcBreak(USERBREAK_PANIC);
	}
	raster->stride = raster->width * natras->bpp;
}

#ifdef RW_3DS

extern void *linearScratch;
extern C3D_Tex whitetex;
	
static GPU_COLORBUF
cameraFormat(GPU_TEXCOLOR tex_fmt)
{
	switch(tex_fmt){
	case GPU_RGBA8:    return GPU_RB_RGBA8;
	case GPU_RGB8:     return GPU_RB_RGB8;
	case GPU_RGBA5551: return GPU_RB_RGBA5551;
	case GPU_RGB565:   return GPU_RB_RGB565;
	case GPU_RGBA4:    return GPU_RB_RGBA4;
	}
	return -1;
}

static Raster*
rasterCreateCameraTexture(Raster *raster, bool tilt)
{
	C3DRaster *natras;
	C3D_TexInitParams params;
	if(raster->format & (Raster::PAL4 | Raster::PAL8)){
		RWERROR((ERR_NOTEXTURE));
		return nil;
	}

	natras = GETC3DRASTEREXT(raster);
	textureFormatMapping(raster, natras);

	int tw; for(tw = 8; tw < raster->width; tw <<= 1);
	int th; for(th = 8; th < raster->height; th <<= 1);
	/* Only the native 400x240 upper-screen camera is stereoscopic.  Reflection
	 * cameras and the 320x240 lower-screen camera remain single-eye targets. */
	const bool stereoTop = tilt && raster->width == 400 && raster->height == 240;
	GPU_TEXCOLOR tex_fmt = natras->format;
	GPU_COLORBUF fbc_fmt = cameraFormat(tex_fmt);

	if(!tilt){
		params = (C3D_TexInitParams){ tw, th, 0, tex_fmt, GPU_TEX_2D, true };
	}else{
		params = (C3D_TexInitParams){ th, tw, 0, tex_fmt, GPU_TEX_2D, true };
	}

	if(fbc_fmt == -1){
		printf("No equivalent GPU_COLORBUF for GPU_TEXCOLOR [%d]\n", tex_fmt);
		svcBreak(USERBREAK_PANIC);
	}

	if(!(natras->tex = rwMallocT(C3D_Tex, 1, MEMDUR_EVENT | ID_DRIVER))){
		printf("rwMalloc / C3D_Tex failed.\n");
		svcBreak(USERBREAK_PANIC);
	}

	if(!C3D_TexInitWithParams(natras->tex, nil, params)){
		printf("TexInitWithParams failed.\n");
		svcBreak(USERBREAK_PANIC);
	}

	natras->fbo = rwMallocT(C3D_FrameBuf, 1, MEMDUR_EVENT | ID_DRIVER);
	assert(natras->fbo);

	if(!tilt){
		/* render to texture */
		C3D_FrameBufAttrib(natras->fbo, tw, th, 0);
	}else{
		/* native camera (rotated) */
		C3D_FrameBufAttrib(natras->fbo, th, tw, 0);
	}

	C3D_FrameBufColor(natras->fbo, natras->tex->data, fbc_fmt);
	C3D_FrameBufDepth(natras->fbo, NULL, GPU_RB_DEPTH24);

	natras->stereoBuf = nil;
	natras->stereoFbo = nil;
	(void)stereoTop; /* right-eye storage is allocated lazily by beginUpdate */

	natras->tilt = tilt;
	natras->numLevels = 1;
	natras->filterMode = 0;
	natras->addressU = 0;
	natras->addressV = 0;
	natras->fboMate = nil;
	natras->onVram = 1;

	return raster;
}

static Raster*
rasterCreateZbuffer(Raster *raster)
{
	C3DRaster *natras = GETC3DRASTEREXT(raster);;
	if(raster->format & (Raster::PAL4 | Raster::PAL8)){
		RWERROR((ERR_NOTEXTURE));
		return nil;
	}

	int w; for (w = 8; w < raster->width; w <<= 1);
	int h; for (h = 8; h < raster->height; h <<= 1);
	u32 size = C3D_CalcDepthBufSize(w, h, GPU_RB_DEPTH24);
	natras->zbuf = vramAlloc(size);
	natras->stereoBuf = nil;
	natras->stereoFbo = nil;

	if(!natras->zbuf){
		printf("not enough vram for zbuffer. Consider blowing up a government building.\n");
		svcBreak(USERBREAK_PANIC);
	}

	natras->numLevels = 1;
	natras->filterMode = 0;
	natras->addressU = 0;
	natras->addressV = 0;
	natras->fbo = nil;
	natras->fboMate = nil;
	natras->onVram = 1;

	return raster;
}

#endif

void
boxFilter(uint8 *src, uint8 *dst, int x, int y, int w, int h)
{
	/* this is kinda embarrasing... hope no real programmers look at this. */
	/* it runs quick enough on my core 2 duo so fuck it. */
	
	int32 r = 0, g = 0, b = 0, a = 0;
	int bx, by, xs, ys;
	uint8 *pix;

	for (by = 0; by < 3; by++){
		for (bx = 0; bx < 3; bx++){
			xs = x + bx - 1;
			ys = y + by - 1;
			if (xs >= 0 && xs < w && ys >= 0 && ys < h){
				pix = &src[(xs * 4) + (ys * w * 4)];
				r += pix[0];
				g += pix[1];
				b += pix[2];
				a += pix[3];
			}
		}
	}

	dst[0] = (uint8)(r / 9);
	dst[1] = (uint8)(g / 9);
	dst[2] = (uint8)(b / 9);
	dst[3] = (uint8)(a / 9);
}

void rasterUnlockWrite(Raster *raster, C3DRaster *natras, int level);

void
texMipmaps(Raster *raster, C3DRaster *natras)
{
	int32        size = raster->stride * raster->height;
	uint8 *old_pixels = raster->pixels;
	uint8        *src = raster->pixels;
	uint8        *dst = src + size;
	uint8 *pix;
	int32 x, y, lev;

	rasterUnlockWrite(raster, natras, 0);
	for(lev = 1; lev < natras->numLevels; lev++){
		assert(raster->width >= 8 && raster->height >= 8);
		pix = dst;
		for(y = 0; y < raster->height; y+=2){
			for(x = 0; x < raster->width; x+=2){
				boxFilter(src, pix, x, y, raster->width, raster->height);
				pix+=4;
			}
		}
			
		raster->width  >>= 1;
		raster->height >>= 1;
		raster->stride >>= 1;

		size >>= 2;
		src    = dst;
		dst   += size;

		raster->pixels = src;
		rasterUnlockWrite(raster, natras, lev);
	}

	raster->pixels = old_pixels;
}

void
texCompress(Raster *raster, C3DRaster *natras, int level)
{
	u32     size = C3D_TexCalcLevelSize(natras->tex->size, level);
	uint8   *dst = getImagePtr(natras->tex, level);
	uint8   *src = raster->pixels;
	uint8   *str = dst;
	uint8   *end = dst + size;
	u32    width = raster->width;
	u32   height = raster->height;
	u32   stride = raster->stride;
	u32    alpha = natras->hasAlpha;

	assert(width >= 8 && height >= 8);
	assert(natras->bpp == 4);
	
	int x, y;
	uint8 *block;
	for (y = height-8; y >= 0; y-=8){
			for (x = 0; x < width; x+=8){
				block = &src[(x * 4) + (y * stride)];
				etc1_common(block, &dst, stride, alpha);
			}
	}

	if(dst != end){
		printf("compression error:\n"
		       "\tsize: %x\tgap: %x\tstart: %p\tdst: %p\tend: %p\n",
		       size, dst - str, str, dst, end);
		svcBreak(USERBREAK_PANIC);
	}
}

void
texSwizzle(Raster *raster, C3DRaster *natras, int level)
{
	uint32     size = C3D_TexCalcLevelSize(natras->tex->size, level);
	uint32    width = raster->width  >> level;
	uint32   height = raster->height >> level;
	uint32     *src = (uint32*)raster->pixels;
	uint32     *dst = (uint32*)getImagePtr(natras->tex, level);

#ifdef RW_3DS
	uint32      dim = GX_BUFFER_DIM(width, height);
	uint32 transfer = natras->transfer;
	uint32    scale = GX_TRANSFER_SCALE_NO;
	uint32    flags = 0;
	
	if (!natras->onVram){
		cpuSwizzle((uint8*)src, (uint8*)dst, width, height, natras->bpp, 0);
		return;
	}

	if (transfer == GX_TRANSFER_FMT_NONE){
		printf("cannot swizzle texture\n");
		svcBreak(USERBREAK_PANIC);
	}

	flags =	GX_TRANSFER_FLIP_VERT(1) |
		GX_TRANSFER_OUT_TILED(1) |
		GX_TRANSFER_RAW_COPY(0) |
		GX_TRANSFER_IN_FORMAT(transfer) |
		GX_TRANSFER_OUT_FORMAT(transfer) |
		GX_TRANSFER_SCALING(scale);

	memcpy(linearScratch, src, size);
	GSPGPU_FlushDataCache(linearScratch, size);
	C3D_SyncDisplayTransfer(linearScratch, dim, dst, dim, flags);

#else
	cpuSwizzle((uint8*)src, (uint8*)dst, width, height, natras->bpp, 0);
#endif	
}

void
texUnswizzle(Raster *raster, C3DRaster *natras)
{
	uint32 *src = (uint32*)getImagePtr(natras->tex, 0);
	uint32 *dst = (uint32*)raster->pixels;
	uint32 size = raster->stride * raster->height;
	
#ifdef RW_3DS
	uint32 dim = GX_BUFFER_DIM(raster->width, raster->height);
	uint32 transfer = natras->transfer;
	uint32 scale = GX_TRANSFER_SCALE_NO;
	
	if (!natras->onVram){
		cpuSwizzle((uint8*)src, (uint8*)dst, raster->width, raster->height, natras->bpp, 1);
		return;
	}

	uint32 flags =
		GX_TRANSFER_FLIP_VERT(1) |
		GX_TRANSFER_OUT_TILED(0) |
		GX_TRANSFER_RAW_COPY(0) |
		GX_TRANSFER_IN_FORMAT(transfer) |
		GX_TRANSFER_OUT_FORMAT(transfer) |
		GX_TRANSFER_SCALING(scale);

	if(transfer == GX_TRANSFER_FMT_NONE){
		printf("[WARNING] cannot unswizzle %p->%p\n", raster, natras);
		svcBreak(USERBREAK_PANIC);
	}

	flags =	GX_TRANSFER_FLIP_VERT(1) |
		GX_TRANSFER_OUT_TILED(0) |
		GX_TRANSFER_RAW_COPY(0) |
		GX_TRANSFER_IN_FORMAT(transfer) |
		GX_TRANSFER_OUT_FORMAT(transfer) |
		GX_TRANSFER_SCALING(scale);

	C3D_SyncDisplayTransfer(src, dim, linearScratch, dim, flags);
	memcpy(dst, linearScratch, size);
#else
	cpuSwizzle((uint8*)src, (uint8*)dst, raster->width, raster->height, natras->bpp, 1);
#endif	
}

static Raster*
rasterCreateTexture(Raster *raster)
{
	C3DRaster *natras = GETC3DRASTEREXT(raster);
	if(raster->format & (Raster::PAL4 | Raster::PAL8)){
		RWERROR((ERR_NOTEXTURE));
		return nil;
	}

	textureFormatMapping(raster, natras);
	GPU_TEXCOLOR fmt = natras->format;
	uint16_t       w = raster->width;
	uint16_t       h = raster->height;
	int       mipmap = raster->format & (Raster::MIPMAP | Raster::AUTOMIPMAP);

	if(!(natras->tex = rwMallocT(C3D_Tex, 1, MEMDUR_EVENT | ID_DRIVER))){
		printf("rwMalloc / C3D_Tex failed.\n");
		svcBreak(USERBREAK_PANIC);
	}

	assert(w >= 8 && h >= 8);
	TexAlloc(raster, w, h, fmt, mipmap);

#ifdef RW_3DS	
	C3D_TexSetFilter(natras->tex, GPU_LINEAR, GPU_LINEAR);
#endif	
	
	natras->autogenMipmap =	(mipmap == (Raster::MIPMAP | Raster::AUTOMIPMAP));
	natras->filterMode    = 0;
	natras->addressU      = 0;
	natras->addressV      = 0;
	natras->fbo           = nil;

	return raster;
}

static Raster*
allocateETC(Raster *raster)
{
	C3DRaster *natras = GETC3DRASTEREXT(raster);
	uint16          w = raster->width;
	uint16          h = raster->height;
	int        mipmap = raster->format & (Raster::MIPMAP | Raster::AUTOMIPMAP);
	bool     hasAlpha = Raster::formatHasAlpha(raster->format);
	GPU_TEXCOLOR fmt = hasAlpha ? GPU_ETC1A4 : GPU_ETC1;
	
	if(!(natras->tex = rwMallocT(C3D_Tex, 1, MEMDUR_EVENT | ID_DRIVER))){
		printf("rwMalloc / C3D_Tex failed.\n");
		svcBreak(USERBREAK_PANIC);
	}

	assert(w >= 8 && h >= 8);

	TexAlloc(raster, w, h, fmt, mipmap);

#ifdef RW_3DS	
	C3D_TexSetFilter(natras->tex, GPU_NEAREST, GPU_NEAREST);
#endif	

	/* these should only be used by locking / unlocking */
	/* therefore the actual values reflect the 32-bit images */
	/* that are used as source material for online compression */
	raster->depth  = 32;
	raster->stride = w * 4;
	natras->bpp    = 4;

	natras->autogenMipmap =	(mipmap == (Raster::MIPMAP | Raster::AUTOMIPMAP));
	natras->format        = fmt;
	natras->hasAlpha      = hasAlpha;
	natras->isCompressed  = 1;
	natras->filterMode    = 0;
	natras->addressU      = 0;
	natras->addressV      = 0;
	natras->fbo           = nil;

	return raster;
}

Raster*
rasterCreate(Raster *raster)
{
	Raster       *ret = raster;
	C3DRaster *natras = GETC3DRASTEREXT(raster);

	natras->isCompressed = 0;
	natras->hasAlpha     = 0;
	natras->hasTranslucentAlpha = 0;
	natras->numLevels    = 1;

	if(raster->width == 0 || raster->height == 0){
		raster->flags |= Raster::DONTALLOCATE;
		raster->stride = 0;
		goto ret;
	}

	if(raster->flags & Raster::DONTALLOCATE)
		goto ret;

	/* force mipmaps */
	if(gForceMipmaps && !(raster->format & Raster::MIPMAP)){
		raster->format |= (Raster::MIPMAP | Raster::AUTOMIPMAP);
	}
	
	switch(raster->type){
	case Raster::NORMAL:
	case Raster::TEXTURE:
		if(gForceCompression){
			ret = allocateETC(raster);
		}else{
			ret = rasterCreateTexture(raster);
		}
		break;
#ifdef RW_3DS		
	case Raster::CAMERATEXTURE:
		ret = rasterCreateCameraTexture(raster, false);
		break;
	case Raster::CAMERA:
		ret = rasterCreateCameraTexture(raster, true);
		break;
	case Raster::ZBUFFER:
		ret = rasterCreateZbuffer(raster);
		break;
#endif
	default:
		RWERROR((ERR_INVRASTER));
		return nil;
	}

ret:
	raster->originalWidth  = raster->width;
	raster->originalHeight = raster->height;
	raster->originalStride = raster->stride;
	raster->originalPixels = raster->pixels;
	return ret;
}

uint8*
rasterLock(Raster *raster, int32 level, int32 lockMode)
{
	C3DRaster *natras = GETC3DRASTEREXT(raster);
	uint8 *px = NULL;
	uint32 i, allocSz;

	assert(raster->privateFlags == 0);

	switch(raster->type){
	case Raster::NORMAL:
	case Raster::TEXTURE:
	case Raster::CAMERATEXTURE:
		raster->width  = raster->originalWidth  >> level;
		raster->height = raster->originalHeight >> level;
		raster->stride = raster->originalStride >> level;

		if(level == 0 && natras->autogenMipmap){
			int size = raster->stride * raster->height;
			int maxLevel = natras->tex->maxLevel;
			allocSz = C3D_TexCalcTotalSize(size, maxLevel);
		}else{
			allocSz = getLevelSize(raster, level);
		}
		// printf("[LOCK] allocSz: %x\n", allocSz);
		px = (uint8*)rwMalloc(allocSz, MEMDUR_EVENT | ID_DRIVER);
		assert(raster->pixels == nil);
		raster->pixels = px;
		raster->privateFlags = lockMode;

		if(lockMode & Raster::LOCKREAD || !(lockMode & Raster::LOCKNOFETCH)){
			printf("[LOCKREAD] converting tiled->linear raster data.\n");
			if(natras->isCompressed){
				printf("(not actually) reading compressed texture...\n"
				       "it is possible however.\n");
				/*rasterblit(...)*/
			}else{
				texUnswizzle(raster, natras);
			}
		}
		break;

	case Raster::CAMERA:
		if(lockMode & Raster::PRIVATELOCK_WRITE){
			printf("can't lock framebuffer for writing");
			svcBreak(USERBREAK_PANIC);
		}
		printf("untested...\n");
		svcBreak(USERBREAK_PANIC);
		// raster->width = c3dGlobals.presentWidth;
		// raster->height = c3dGlobals.presentHeight;
		// raster->stride = raster->width*natras->bpp;
		// assert(natras->bpp == 4);
		// allocSz = raster->height*raster->stride;
		// px = (uint8*)rwMalloc(allocSz, MEMDUR_EVENT | ID_DRIVER);
		// texUnswizzle(raster, natras);
		// raster->pixels = px;
		// raster->privateFlags = lockMode;
		break;

	default:
		printf("cannot lock this type of raster yet");
		svcBreak(USERBREAK_PANIC);
	}

	if(!px){
		printf("out of memory?\n");
		svcBreak(USERBREAK_PANIC);
	}
	return px;
}

void
rasterUnlockWrite(Raster *raster, C3DRaster *natras, int level)
{
	if(natras->isCompressed){
		texCompress(raster, natras, level);
	}else{
		texSwizzle(raster, natras, level);
	}
}

void
rasterUnlock(Raster *raster, int32 level)
{
	C3DRaster *natras = GETC3DRASTEREXT(raster);

	assert(raster->pixels);

	switch(raster->type){
	case Raster::NORMAL:
	case Raster::TEXTURE:
	case Raster::CAMERATEXTURE:
		if(raster->privateFlags & Raster::LOCKWRITE){
			if(level == 0 && natras->autogenMipmap){
				texMipmaps(raster, natras);
			}else{
				if (!raster->pixels){
					printf("rasterUnlock: raster->pixels == NULL\n");
					svcBreak(USERBREAK_PANIC);
				}else{
					rasterUnlockWrite(raster, natras, level);
				}
			}
		}
		break;

	case Raster::CAMERA:
		svcBreak(USERBREAK_PANIC);
		break;
	}

	// memoryInfo(); /* caught a leak using this highly advanced technique */
	rwFree(raster->pixels);
	raster->pixels = nil;

	raster->width = raster->originalWidth;
	raster->height = raster->originalHeight;
	raster->stride = raster->originalStride;
	raster->pixels = raster->originalPixels;
	raster->privateFlags = 0;
}

int32
rasterNumLevels(Raster *raster)
{
	return GETC3DRASTEREXT(raster)->numLevels;
}

// Almost the same as d3d9 and ps2 function
bool32
imageFindRasterFormat(Image *img, int32 type,
	int32 *pWidth, int32 *pHeight, int32 *pDepth, int32 *pFormat)
{
	int32 width, height, depth, format;

	assert((type&0xF) == Raster::TEXTURE);

	// Perhaps non-power-of-2 textures are acceptable?
	width  = IMAX(8, img->width);
	height = IMAX(8, img->height);
	depth  = img->depth;

	if(depth <= 8)
		depth = 32;

	switch(depth){
	case 32:
		if(img->hasAlpha())
			format = Raster::C8888;
		else{
			format = Raster::C888;
			depth = 24;
		}
		break;
	case 24:
		format = Raster::C888;
		break;
	case 16:
		format = Raster::C1555;
		break;

	case 8:
	case 4:
	default:
		RWERROR((ERR_INVRASTER));
		return 0;
	}

	format  |= type;
	
	*pWidth  = width;
	*pHeight = height;
	*pDepth  = depth;
	*pFormat = format;

	return 1;
}

static void
imageConvert(Image *image, Raster *raster, void (*conv)(uint8 *out, uint8 *in))
{
	C3DRaster *natras = GETC3DRASTEREXT(raster);
	uint8  *raspixels = raster->pixels;
	uint8  *imgpixels = image->pixels;
	int x, y;
	assert(image->width == raster->width);
	assert(image->height == raster->height);
	for(y = 0; y < image->height; y++){
		uint8 *rasrow = raspixels;
		uint8 *imgrow = imgpixels;
		for(x = 0; x < image->width; x++){
			conv(rasrow, imgrow);
			rasrow += natras->bpp;
			imgrow += image->bpp;
		}
		raspixels += raster->stride;
		imgpixels += image->stride;
	}
}

bool32
rasterFromImage(Raster *raster, Image *image)
{
	if((raster->type&0xF) != Raster::TEXTURE)
		return 0;

	C3DRaster *natras = GETC3DRASTEREXT(raster);
	int32      format = raster->format & 0xF00;
	
	// Unpalettize image if necessary but don't change original
	Image *truecolimg = nil;
	void (*conv)(uint8 *out, uint8 *in) = nil;

	if(natras->isCompressed){	
		truecolimg          = Image::create(image->width, image->height, image->depth);
		truecolimg->pixels  = image->pixels;
		truecolimg->stride  = image->stride;
		truecolimg->palette = image->palette;
		truecolimg->convertTo32();
		image = truecolimg;
	}else if(image->depth <= 8){
		truecolimg          = Image::create(image->width, image->height, image->depth);
		truecolimg->pixels  = image->pixels;
		truecolimg->stride  = image->stride;
		truecolimg->palette = image->palette;
		truecolimg->unpalettize();
		image = truecolimg;
	}
	
	if(!natras->isCompressed){
		switch(image->depth){
		case 32:
			if(format == Raster::C8888)
				conv = conv_ABGR8888_from_RGBA8888;
			else if(format == Raster::C888)
				conv = conv_BGR888_from_RGB888;
			else
				goto err;
			break;
		case 24:
			if(format == Raster::C8888)
				conv = conv_ABGR8888_from_RGB888;
			else if(format == Raster::C888)
				conv = conv_BGR888_from_RGB888;
			else
				goto err;
			break;
		case 16:
			if(format == Raster::C1555)
				conv = conv_ARGB1555_from_RGBA5551;
			else
				goto err;
			break;

		case 8:
		case 4:
		default:
		err:
			RWERROR((ERR_INVRASTER));
			return 0;
		}
	}

	if (image->width < 8 || image->height < 8){
		image->upscale(8);
	}

	bool unlock = false;
	if(raster->pixels == nil){
		raster->lock(0, Raster::LOCKWRITE|Raster::LOCKNOFETCH);
		unlock = true;
	}

	if(!natras->isCompressed){
		imageConvert(image, raster, conv);
	}else{
		int size = raster->stride * raster->height;
		// printf("[rasterFromImage] size: %x\n", size);
		memcpy(raster->pixels, image->pixels, size);
	}

	if(unlock){
		raster->unlock(0);
	}

	if(truecolimg){
		truecolimg->destroy();
	}

	return 1;
}

Image*
rasterToImage(Raster *raster)
{
	int32 depth;
	Image *image;

	bool unlock = false;
	if(raster->pixels == nil){
		raster->lock(0, Raster::LOCKREAD);
		unlock = true;
	}

	C3DRaster *natras = GETC3DRASTEREXT(raster);
	if(natras->isCompressed){
		// TODO
		RWERROR((ERR_INVRASTER));
		return nil;
	}

	void (*conv)(uint8 *out, uint8 *in) = nil;
	switch(raster->format & 0xF00){
	case Raster::C1555:
		depth = 16;
		conv = conv_ARGB1555_from_RGBA5551;
		break;
	case Raster::C8888:
		depth = 32;
		conv = conv_RGBA8888_from_RGBA8888;
		break;
	case Raster::C888:
		depth = 24;
		conv = conv_RGB888_from_RGB888;
		break;

	default:
	case Raster::C555:
	case Raster::C565:
	case Raster::C4444:
	case Raster::LUM8:
		RWERROR((ERR_INVRASTER));
		return nil;
	}

	if(raster->format & Raster::PAL4 ||
	   raster->format & Raster::PAL8){
		RWERROR((ERR_INVRASTER));
		return nil;
	}

	// uint8 *in, *out;
	image = Image::create(raster->width, raster->height, depth);
	image->allocate();

	uint8 *imgpixels = image->pixels + (image->height-1)*image->stride;
	uint8 *pixels = raster->pixels;

	int x, y;
	assert(image->width == raster->width);
	assert(image->height == raster->height);
	for(y = 0; y < image->height; y++){
		uint8 *imgrow = imgpixels;
		uint8 *rasrow = pixels;
		for(x = 0; x < image->width; x++){
			conv(imgrow, rasrow);
			imgrow += image->bpp;
			rasrow += natras->bpp;
		}
		imgpixels -= image->stride;
		pixels += raster->stride;
	}

	if(unlock)
		raster->unlock(0);

	return image;
}

static void*
createNativeRaster(void *object, int32 offset, int32)
{
	C3DRaster *ras = PLUGINOFFSET(C3DRaster, object, offset);
#ifdef RW_3DS
	ras->tex = nil;
	ras->fbo = nil;
	ras->stereoBuf = nil;
	ras->stereoFbo = nil;
	ras->lastUsedSerial = 0;
	ras->shrinkProtected = false;
#endif	
	ras->fboMate = nil;
	return object;
}

static void*
copyNativeRaster(void *dst, void *, int32 offset, int32)
{
	C3DRaster *d = PLUGINOFFSET(C3DRaster, dst, offset);
#ifdef RW_3DS       
	d->tex = 0;
	d->fbo = 0;
	d->stereoBuf = nil;
	d->stereoFbo = nil;
	d->lastUsedSerial = 0;
	d->shrinkProtected = false;
#endif	
	d->fboMate = nil;
	return dst;
}

static void*
destroyNativeRaster(void *object, int32 offset, int32)
{
	Raster *raster = (Raster*)object;
	C3DRaster *natras = PLUGINOFFSET(C3DRaster, object, offset);
#ifdef RW_3DS
	/* The device cache stores raw Raster pointers. Clear every stage before
	 * this native texture and its owning object can become dangling. */
	forgetRasterState(raster);
	switch(raster->type){
	case Raster::NORMAL:
	case Raster::TEXTURE:
		if(natras->tex){
			TexFree(natras);
		}
		break;

	case Raster::CAMERA:
	case Raster::CAMERATEXTURE:
		if(natras->fboMate){
			C3DRaster *zras = GETC3DRASTEREXT(natras->fboMate);
			zras->fboMate = nil;
			natras->fboMate = nil;
		}
		if (natras->tex){
			TexFree(natras);
		}
		if(natras->stereoBuf)
			vramFree(natras->stereoBuf);
		if(natras->stereoFbo)
			rwFree(natras->stereoFbo);
		rwFree(natras->fbo);
		break;

	case Raster::ZBUFFER:
		if(natras->fboMate){
			// Detatch from FBO we may be attached to
			C3DRaster *oldfb = GETC3DRASTEREXT(natras->fboMate);
			if(oldfb->fbo){
				C3D_FrameBufDepth(oldfb->fbo, nil, GPU_RB_DEPTH24);
			}
			if(oldfb->stereoFbo)
				C3D_FrameBufDepth(oldfb->stereoFbo, nil,
				                  GPU_RB_DEPTH24);
			oldfb->fboMate = nil;
		}
		if(natras->zbuf){
			vramFree(natras->zbuf);
		}
		if(natras->stereoBuf)
			vramFree(natras->stereoBuf);
		break;
	}

	natras->tex = 0;
	natras->fbo = 0;
	natras->stereoBuf = nil;
	natras->stereoFbo = nil;
#endif
	return object;
}

Texture*
readNativeTexture(Stream *stream)
{
	uint32 platform;
	if(!findChunk(stream, ID_STRUCT, nil, nil)){  // txd:12
		RWERROR((ERR_CHUNK, "STRUCT"));
		return nil;
	}
	platform = stream->readU32();                 // txd:16
	if(platform != PLATFORM_3DS){
		RWERROR((ERR_PLATFORM, platform));
		return nil;
	}
	Texture *tex = Texture::create(nil);
	if(tex == nil)
		return nil;

	// Texture
	tex->filterAddressing = stream->readU32();    // txd:20
	stream->read8(tex->name, 32);		      // txd:52
	stream->read8(tex->mask, 32);		      // txd:84

	// Raster
	uint32   format = stream->readU32();          // txd:88
	int32     width = stream->readI32();          // txd:92
	int32    height = stream->readI32();          // txd:96
	int32     depth = stream->readI32();          // txd:100
	int32 numLevels = stream->readI32();          // txd:104

	// Native raster
	int32 flags     = stream->readI32();          // txd:108
	Raster *raster;
	C3DRaster *natras;
	if(flags & 2){
		raster = Raster::create(width, height, depth,
					format | Raster::TEXTURE | Raster::DONTALLOCATE, PLATFORM_3DS);
		// allocateETC(raster, flags & 1);
		allocateETC(raster);
	}else{
		raster = Raster::create(width, height, depth,
					format | Raster::TEXTURE, PLATFORM_3DS);
	}
	assert(raster);
	natras = GETC3DRASTEREXT(raster);

	if(flags & 1){
		if(!natras->hasAlpha){
			printf("raster should have alpha\n");
			svcBreak(USERBREAK_PANIC);
		}
	}
	
	tex->raster = raster;

	uint32 size;
	void *data;
	
	size = stream->readU32();             // txd:112
	data = natras->tex->data;

#ifndef NDEBUG	
	if(size != natras->totalSize){
		printf("[c3d:readNativeTexture] - size mismatch!\n"
		       "\ton-disk: %x\tcalculated: %x\n",
		       size,
		       C3D_TexCalcTotalSize(natras->tex->size,
					    natras->tex->maxLevel));
		svcBreak(USERBREAK_PANIC);
	}
#endif	

#ifdef RW_3DS
	// if(natras->onVram){ /* its all so tiresome ... */
	// 	stream->read8(linearScratch, size);
	// 	GSPGPU_FlushDataCache(linearScratch, size);
	// 	C3D_SyncTextureCopy(linearScratch, 0, (u32*)data, 0, size, 8);
	// 	C3D_FrameSplit(0);
	// }else
#endif
	{
		stream->read8(data, size);
	}
	natras->hasTranslucentAlpha = hasIntermediateAlpha(natras, (const uint8*)data, size);

	return tex;
}

void
writeNativeTexture(Texture *tex, Stream *stream)
{
	Raster *raster = tex->raster;
	C3DRaster *natras = GETC3DRASTEREXT(raster);

	int32 chunksize = getSizeNativeTexture(tex);
	writeChunkHeader(stream, ID_STRUCT, chunksize-12); // txd:12
	stream->writeU32(PLATFORM_3DS);			   // txd:16

	// Texture
	stream->writeU32(tex->filterAddressing);           // txd:20
	stream->write8(tex->name, 32);			   // txd:52
	stream->write8(tex->mask, 32);			   // txd:84

	// Raster
	int32 numLevels = natras->numLevels;
	stream->writeI32(raster->format);                  // txd:88
	stream->writeI32(raster->width);                   // txd:92
	stream->writeI32(raster->height);                  // txd:96
	stream->writeI32(raster->depth);                   // txd:100
	stream->writeI32(numLevels);			   // txd:104

	// Native raster
	int32 flags = 0;
	if(natras->hasAlpha){
		flags |= 1;
	}if(natras->isCompressed){
		flags |= 2;
	}
	stream->writeI32(flags);                           // txd:108

	uint32 size;
	uint8 *data;

	size = natras->totalSize;
	data = natras->tex->data;
	stream->writeU32(size);
	
#ifdef RW_3DS
	if(natras->onVram){
		C3D_SyncTextureCopy((u32*)data, 0, linearScratch, 0, size, 8);
		data = linearScratch;
	}
#endif
	
	stream->write8(data, size);
}

uint32
getSizeNativeTexture(Texture *tex)
{
	Raster       *ras = tex->raster;
	C3DRaster *natras = GETC3DRASTEREXT(ras);
	return 112 + natras->totalSize;
}

void registerNativeRaster(void)
{
	nativeRasterOffset = Raster::registerPlugin(sizeof(C3DRaster),
						    ID_RASTERC3D,
						    createNativeRaster,
						    destroyNativeRaster,
						    copyNativeRaster);
}

}
}
