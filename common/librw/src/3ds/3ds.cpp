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
#include "rw3dsshader.h"
#include "rw3dsimpl.h"

namespace rw {
namespace c3d {

static void*
driverOpen(void *o, int32, int32)
{
#ifdef RW_3DS
  engine->driver[PLATFORM_3DS]->defaultPipeline = makeDefaultPipeline();
#endif
	engine->driver[PLATFORM_3DS]->rasterNativeOffset = nativeRasterOffset;
	engine->driver[PLATFORM_3DS]->rasterCreate       = rasterCreate;
	engine->driver[PLATFORM_3DS]->rasterLock         = rasterLock;
	engine->driver[PLATFORM_3DS]->rasterUnlock       = rasterUnlock;
	engine->driver[PLATFORM_3DS]->rasterNumLevels    = rasterNumLevels;
	engine->driver[PLATFORM_3DS]->imageFindRasterFormat = imageFindRasterFormat;
	engine->driver[PLATFORM_3DS]->rasterFromImage    = rasterFromImage;
	engine->driver[PLATFORM_3DS]->rasterToImage      = rasterToImage;

	return o;
}
  
static void*
driverClose(void *o, int32, int32)
{
	return o;
}

void
registerPlatformPlugins(void)
{
	Driver::registerPlugin(PLATFORM_3DS, 0, PLATFORM_3DS,
	                       driverOpen, driverClose);
	registerNativeRaster();
}

}
}
