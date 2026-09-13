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

typedef
struct LListNode_t
{
	C3D_Tex *tex;
	struct LListNode_t *cdr;
} LListNode;

static LListNode *gTexPool = NULL;

LListNode*
LListNode_cons(C3D_Tex *tex)
{
	LListNode *node = malloc(sizeof(LListNode));
	node->tex = tex;
	node->cdr = NULL;
	return node;
}

LListNode*
TexPoolRemove(C3D_Tex *tex)
{
	LListNode **prev = &gTexPool;
	LListNode *curNode = gTexPool;
	while(curNode){
		if(curNode->tex == tex){
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
		total += node->tex->size;
		printf("%x:\tsize:\t%x\tmaxLevel:\t%d\n",
		       node, node->tex->size, node->tex->maxLevel);
	}
	printf("total: %d Mb / %d Kb\n", total>>20, total>>10);
	printf("free: %d Mb / %d Kb\n", free>>20, free>>10);
}
	
LListNode*
TexPoolPop()
{
	if (!gTexPool){
		return NULL;
	}
	
	LListNode *node = gTexPool;
	gTexPool = node->cdr;
	node->cdr = NULL;
	return node;
}

void
TexPoolInsert(LListNode *node)
{
	LListNode **prev = &gTexPool;
	LListNode *curNode = gTexPool;

	while(curNode){
		if(node->tex->size < curNode->tex->size){
			prev = &curNode->cdr;
			curNode = curNode->cdr;
		}else{
			node->cdr = curNode;
			break;
		}
	}

	*prev = node;
}

static size_t shrinkSomeTexture();
	
void*
safeLinearAlloc(size_t size)
{
	void *ptr;
	
	while(linearSpaceFree() < size * 2){
		shrinkSomeTexture();
	}

	if(!(ptr = linearAlloc(size))){
		printf("fuck you\n");
		svcBreak(USERBREAK_PANIC);
	}

	return ptr;
}
	
static inline size_t
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

static size_t
shrinkSomeTexture()
{
	// TexPoolDebug();
	
	LListNode *node = TexPoolPop();

	if(!node){
		return 0;
	}
	
	C3D_Tex *tex = node->tex;
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

	/* what I want: (modified libctru linear allocator) */
	// myLinearPrefixFree(tex->data, oldSizeBase);

	/* what seems to work: */
	memcpy(linearScratch, srcData, newSizeTotal);
	linearFree(tex->data);
	tex->data = linearAlloc(newSizeTotal);
	assert(tex->data);
	memcpy(tex->data, linearScratch, newSizeTotal);

	if(tex->maxLevel > 0){
		TexPoolInsert(node);
	}else{
		free(node);
	}

	return oldSizeBase;
}

void
TexFree(C3DRaster *natras)
{
	C3D_Tex *tex = natras->tex;
	void *ptr = tex->data;
	
	if(natras->onVram){
		vramFree(ptr);
	}else{
		LListNode *node = TexPoolRemove(tex);
		// TexPoolDebug();
		free(node);
		linearFree(ptr);
	}

	rwFree(tex);
}

int
TexAlloc(Raster *raster, int w, int h, GPU_TEXCOLOR fmt, int mipmap)
{
	C3DRaster     *natras = GETC3DRASTEREXT(raster);
	int            maxlev = (!mipmap) ? 0 : C3D_TexCalcMaxLevel(w, h);
	bool transfer_is_safe = (w * h) > (64 * 64);
	
	C3D_TexInitParams params =
		{ w, h, maxlev, fmt, GPU_TEX_2D, false };

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
		/* very ad-hoc memory allocation policy */
		int space = linearSpaceFree();
		
		if(space < (16<<20)){
			shrinkSomeTexture();
		}else if(space < (8<<20)){
			while(linearSpaceFree() < (8<<20)){
				if(!shrinkSomeTexture()){
					break;
				}
			}
		}
		
		if(!C3D_TexInitWithParams(natras->tex, nil, params)){
			printf("no more tricks up our sleeve.\n");
			svcBreak(USERBREAK_PANIC);
		}

		if(params.maxLevel){
			TexPoolInsert(LListNode_cons(natras->tex));
			// TexPoolDebug();
		}
	}

	natras->totalSize = C3D_TexCalcTotalSize(natras->tex->size, params.maxLevel);
	natras->numLevels = params.maxLevel + 1;
	natras->onVram    = params.onVram;

	// int free = linearSpaceFree();
	// printf("[TexAlloc] "
	//        "%3dx%3d\t"
	//        "levels: %d\t"
	//        "size: %dkb\t"
	//        "free: %dmb / %dkb\n",
	//        raster->width, raster->height, natras->numLevels,
	//        natras->totalSize>>10,
	//        free >> 20, free >> 10);
	
	return 1;
}

}
}
