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

namespace rw {
namespace c3d {

#ifdef RW_3DS
  
typedef
struct LListNode_t
{
	C3DRaster *natras;
	struct LListNode_t *cdr;
} LListNode;

static LListNode *gTexPool = NULL;
static uint32 gTextureUseSerial = 0;
static TextureMemoryPressureCallback gMemoryPressureCallback = NULL;

LListNode*
LListNode_cons(C3DRaster *natras)
{
	LListNode *node = malloc(sizeof(LListNode));
	node->natras = natras;
	node->cdr = NULL;
	return node;
}

LListNode*
TexPoolRemove(C3DRaster *natras)
{
	LListNode **prev = &gTexPool;
	LListNode *curNode = gTexPool;
	while(curNode){
		if(curNode->natras == natras){
			*prev = curNode->cdr;
			return curNode;
		}else{
			prev = &curNode->cdr;
			curNode = curNode->cdr;
		}
	}
	return NULL;
}

void
TexPoolDebug()
{
	size_t free = linearSpaceFree();
	size_t total = 0;
	LListNode *node = gTexPool;
	for(; node; node=node->cdr){
		C3D_Tex *tex = node->natras->tex;
		total += tex->size;
		printf("%x:\tsize:\t%x\tmaxLevel:\t%d\n",
		       node, tex->size, tex->maxLevel);
	}
	printf("total: %d Mb / %d Kb\n", total>>20, total>>10);
	printf("free: %d Mb / %d Kb\n", free>>20, free>>10);
}
	
void
TexPoolInsert(LListNode *node)
{
	node->cdr = gTexPool;
	gTexPool = node;
}

void
TexTouch(C3DRaster *natras)
{
	if(natras)
		natras->lastUsedSerial = ++gTextureUseSerial;
}

void
TexSetShrinkProtected(C3DRaster *natras)
{
	if(natras)
		natras->shrinkProtected = true;
}

void
TexSetMemoryPressureCallback(TextureMemoryPressureCallback callback)
{
	gMemoryPressureCallback = callback;
}

static size_t shrinkSomeTexture();
	
void*
safeLinearAlloc(size_t size)
{
	void *ptr;
	int attempts = 0;

	/* Vertex/index buffers share the same linear heap as textures. Reclaim a
	 * complete streamed asset first here as well, otherwise a geometry upload
	 * can still irreversibly downscale unrelated textures behind TexAlloc's
	 * improved policy. */
	while(linearSpaceFree() < size * 2 && gMemoryPressureCallback &&
	      attempts++ < 128 && gMemoryPressureCallback(size * 2)){}
	while(linearSpaceFree() < size * 2){
		if(!shrinkSomeTexture())
			break;
	}

	if(!(ptr = linearAlloc(size))){
		printf("fuck you\n");
		svcBreak(USERBREAK_PANIC);
	}

	return ptr;
}

static size_t
shrinkSomeTexture()
{
	// TexPoolDebug();
	
	/* Shrinking is destructive: the discarded top mip cannot be reconstructed.
	 * Pick the least recently used ordinary texture instead of the largest one.
	 * Vehicle textures are never candidates: vehicle geometry LOD, hardware mip
	 * selection and TXD streaming already provide reversible distance scaling. */
	LListNode **bestPrev = NULL;
	LListNode *best = NULL;
	uint32 bestAge = 0;
	LListNode **prev = &gTexPool;
	for(LListNode *cur = gTexPool; cur; cur = cur->cdr){
		C3DRaster *natras = cur->natras;
		uint32 age = gTextureUseSerial - natras->lastUsedSerial;
		if(!natras->shrinkProtected &&
		   (best == NULL || age > bestAge ||
		    (age == bestAge && natras->tex->size > best->natras->tex->size))){
			best = cur;
			bestPrev = prev;
			bestAge = age;
		}
		prev = &cur->cdr;
	}

	if(!best){
		return 0;
	}
	*bestPrev = best->cdr;
	best->cdr = NULL;
	
	C3DRaster *natras = best->natras;
	C3D_Tex *tex = natras->tex;
	assert(tex);
	assert(tex->maxLevel > 0);

	int newMaxLevel = tex->maxLevel - 1;
	int newWidth = tex->width / 2;
	int newHeight = tex->height / 2;

	u32 oldSizeBase = tex->size;
	u32 newSizeBase = newWidth * newHeight * fmtSize(tex->fmt) / 8;
	u32 newSizeTotal = C3D_TexCalcTotalSize(newSizeBase, newMaxLevel);
	void *srcData = C3D_TexGetImagePtr(tex, tex->data, 1, NULL);

	/* natras->totalSize = newSizeTotal; ??? */
	tex->size = newSizeBase;
	tex->width = newWidth;
	tex->height = newHeight;
	tex->maxLevel = newMaxLevel;
	natras->totalSize = newSizeTotal;
	natras->numLevels = newMaxLevel + 1;

	/* what I want: (modified libctru linear allocator) */
	// myLinearPrefixFree(tex->data, oldSizeBase);

	/* what seems to work: */
	memcpy(linearScratch, srcData, newSizeTotal);
	linearFree(tex->data);
	tex->data = linearAlloc(newSizeTotal);
	assert(tex->data);
	memcpy(tex->data, linearScratch, newSizeTotal);

	if(tex->maxLevel > 0){
		TexPoolInsert(best);
	}else{
		free(best);
	}

	return oldSizeBase;
}

#endif  
	
size_t
fmtSize(GPU_TEXCOLOR fmt)
{
	switch (fmt)
	{
		case GPU_RGBA8:
			return 32;
		case GPU_RGB8:
			return 24;
		case GPU_RGBA5551:
		case GPU_RGB565:
		case GPU_RGBA4:
		case GPU_LA8:
		case GPU_HILO8:
			return 16;
		case GPU_L8:
		case GPU_A8:
		case GPU_LA4:
		case GPU_ETC1A4:
			return 8;
		case GPU_L4:
		case GPU_A4:
		case GPU_ETC1:
			return 4;
		default:
			return 0;
	}
}
  
int
TexAlloc(Raster *raster, int w, int h, GPU_TEXCOLOR fmt, int mipmap)
{
	C3DRaster     *natras = GETC3DRASTEREXT(raster);
	int          maxLevel = (!mipmap) ? 0 : C3D_TexCalcMaxLevel(w, h);
	bool           onVram = false; //(w * h) > (64 * 64);

#ifdef RW_3DS
	C3D_TexInitParams params =
		{ w, h, maxLevel, fmt, GPU_TEX_2D, false };

	/* vram */
	// I basically gave up on troubleshooting VRAM.
	// We could get an extra 4MB or so...
	// if(params.onVram){
	// 	if(!C3D_TexInitWithParams(natras->tex, nil, params)){
	// 		params.onVram = 0; /* lets try linear instead */
	// 	}
	// }

	/* linear */
	if(!params.onVram){
		const size_t textureSize = C3D_TexCalcTotalSize(
			(w * h * fmtSize(fmt)) / 8, maxLevel);
		const size_t emergencyReserve = 8 << 20;
		const size_t targetFree = textureSize + emergencyReserve;

		/* Prefer throwing away complete streamed resources. They can be read back
		 * at full quality, unlike shrinkSomeTexture(), which permanently discards
		 * the top mip level. The game callback removes one LRU model/TXD per call. */
		int attempts = 0;
		while(linearSpaceFree() < targetFree && gMemoryPressureCallback &&
		      attempts++ < 128 && gMemoryPressureCallback(targetFree)){}

		/* Keep destructive shrinking as a last-resort compatibility fallback for
		 * allocations that happen before streaming is ready or after all ordinary
		 * resources have genuinely been exhausted. */
		while(linearSpaceFree() < targetFree && shrinkSomeTexture()){}

		bool allocated = C3D_TexInitWithParams(natras->tex, nil, params);
		attempts = 0;
		while(!allocated && gMemoryPressureCallback && attempts++ < 128 &&
		      gMemoryPressureCallback(targetFree))
			allocated = C3D_TexInitWithParams(natras->tex, nil, params);
		while(!allocated && shrinkSomeTexture())
			allocated = C3D_TexInitWithParams(natras->tex, nil, params);

		if(!allocated){
			printf("no more tricks up our sleeve.\n");
			svcBreak(USERBREAK_PANIC);
		}

		if(maxLevel){
			TexPoolInsert(LListNode_cons(natras));
			// TexPoolDebug();
		}
	}

#else
	natras->tex->width    = w;
	natras->tex->height   = h;
	natras->tex->fmt      = fmt;
	natras->tex->minLevel = 0;
	natras->tex->maxLevel = maxLevel;
	natras->tex->size = (w * h * fmtSize(fmt)) / 8;
	natras->tex->data = malloc(C3D_TexCalcTotalSize(natras->tex->size, maxLevel));
#endif
	natras->totalSize = C3D_TexCalcTotalSize(natras->tex->size, maxLevel);
	natras->numLevels = maxLevel + 1;
	natras->onVram    = onVram;

	return 1;
}

void
TexFree(C3DRaster *natras)
{
	C3D_Tex *tex = natras->tex;
	void *ptr = tex->data;

#ifdef RW_3DS	
	if(natras->onVram){
		vramFree(ptr);
	}else{
		LListNode *node = TexPoolRemove(natras);
		free(node);
		linearFree(ptr);
	}
#else
	free(ptr);
#endif	

	rwFree(tex);
}

}
}
