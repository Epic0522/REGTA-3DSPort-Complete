namespace rw {
namespace c3d {
inline void profileRecordDraw(uint32, ProfileDrawClass) {}

#ifdef RW_3DS

void openIm2D(void);
void closeIm2D(void);
void im2DRenderLine(void *vertices, int32 numVertices,
  int32 vert1, int32 vert2);
void im2DRenderTriangle(void *vertices, int32 numVertices,
  int32 vert1, int32 vert2, int32 vert3);
void im2DRenderPrimitive(PrimitiveType primType,
   void *vertices, int32 numVertices);
void im2DRenderIndexedPrimitive(PrimitiveType primType,
   void *vertices, int32 numVertices, void *indices, int32 numIndices);
void im2DRenderBlit();
  
void openIm3D(void);
void closeIm3D(void);
void im3DTransform(void *vertices, int32 numVertices, Matrix *world, uint32 flags);
void im3DRenderPrimitive(PrimitiveType primType);
void im3DRenderIndexedPrimitive(PrimitiveType primType, void *indices, int32 numIndices);
void im3DEnd(void);

/* The upper screen uses a vertically stacked pair of render targets.  World
 * geometry selects an eye projection as well as its viewport; 2D overlays
 * only select the viewport so they remain on the screen plane. */
bool32 stereoRenderActive(void);
int32 stereoRenderPassCount(void);
int32 stereoRenderEye(int32 pass);
void setStereoEye(int32 eye);
void setStereoEyeViewport(int32 eye);
struct DisplayMode
{
  int wide_mode;
  GSPGPU_FramebufferFormat format;
};

struct C3DGlobals
{
	DisplayMode *modes;
	int numModes;
	int currentMode;
	int currentDisplay;
	int presentWidth, presentHeight;
	int presentOffX, presentOffY;
	
	// for opening the window
	int winWidth, winHeight;
	const char *winTitle;
	uint32 numSamples;
};

extern C3DGlobals c3dGlobals;
#endif

Raster *rasterCreate(Raster *raster);
uint8 *rasterLock(Raster*, int32 level, int32 lockMode);
void rasterUnlock(Raster*, int32);
int32 rasterNumLevels(Raster*);
bool32 imageFindRasterFormat(Image *img, int32 type,
	int32 *width, int32 *height, int32 *depth, int32 *format);
bool32 rasterFromImage(Raster *raster, Image *image);
Image *rasterToImage(Raster *raster);

}
}
