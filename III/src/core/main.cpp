#include "common.h"
#include <time.h>
#include "rpmatfx.h"
#include "rphanim.h"
#include "rpskin.h"
#include "rtbmp.h"
#include "rtpng.h"
#if defined(_3DS) && defined(ENABLE_3DS_BOTTOM_RADAR)
#include "lodepng/lodepng.h"
#include "bottom_touch_bin.h"
#include "bottom_menu_map_bin.h"
#endif
#ifdef ANISOTROPIC_FILTERING
#include "rpanisot.h"
#endif

#include "main.h"
#include "CdStream.h"
#include "General.h"
#include "RwHelper.h"
#include "Clouds.h"
#include "Draw.h"
#include "Sprite2d.h"
#include "Renderer.h"
#include "Coronas.h"
#include "WaterLevel.h"
#include "Weather.h"
#include "Glass.h"
#include "WaterCannon.h"
#include "SpecialFX.h"
#include "Shadows.h"
#include "Skidmarks.h"
#include "Antennas.h"
#include "Rubbish.h"
#include "Particle.h"
#include "Pickups.h"
#include "WeaponEffects.h"
#include "PointLights.h"
#include "Fluff.h"
#include "Replay.h"
#include "Camera.h"
#include "World.h"
#include "Ped.h"
#include "PlayerInfo.h"
#include "PlayerPed.h"
#include "Radar.h"
#include "WeaponInfo.h"
#include "Font.h"
#include "Pad.h"
#include "Hud.h"
#include "User.h"
#include "Messages.h"
#include "Darkel.h"
#include "Garages.h"
#include "MusicManager.h"
#include "VisibilityPlugins.h"
#include "NodeName.h"
#include "DMAudio.h"
#include "CutsceneMgr.h"
#include "Lights.h"
#include "Credits.h"
#include "ZoneCull.h"
#include "Timecycle.h"
#include "TxdStore.h"
#include "FileMgr.h"
#include "Text.h"
#include "RpAnimBlend.h"
#include "Frontend.h"
#include "AnimViewer.h"
#include "Script.h"
#include "PathFind.h"
#include "Debug.h"
#include "Console.h"
#include "timebars.h"
#include "GenericGameStorage.h"
#include "MemoryCard.h"
#include "SceneEdit.h"
#include "debugmenu.h"
#include "Clock.h"
#include "postfx.h"
#include "custompipes.h"
#include "screendroplets.h"
#include "MemoryHeap.h"
#if defined(_3DS) && (defined(ENABLE_3DS_BOTTOM_LOADING) || defined(ENABLE_3DS_BOTTOM_RADAR))
#include <3ds.h>
#endif
#if defined(_3DS) && defined(ENABLE_3DS_BOTTOM_LOADING)
#include <3ds/console.h>
#endif
#ifdef USE_OUR_VERSIONING
#include "GitSHA1.h"
#endif

GlobalScene Scene;

uint8 work_buff[55000];
char gString[256];
char gString2[512];
wchar gUString[256];
wchar gUString2[256];

float FramesPerSecond = 30.0f;

bool gbPrintShite = false;
bool gbModelViewer;
#ifdef TIMEBARS
bool gbShowTimebars;
#endif
#ifdef DRAW_GAME_VERSION_TEXT
bool gDrawVersionText; // Our addition, we think it was always enabled on !MASTER builds
#endif

volatile int32 frameCount;

RwRGBA gColourTop;

bool gameAlreadyInitialised;

float NumberOfChunksLoaded;
#ifdef GTA_PS2
#define TOTALNUMCHUNKS 48.0f
#else
#define TOTALNUMCHUNKS 73.0f
#endif

#if defined(_3DS) && defined(ENABLE_3DS_BOTTOM_LOADING)
namespace {
enum { BOTTOM_WIDTH = 320, BOTTOM_HEIGHT = 240, BOTTOM_BPP = 3 };
static bool BottomLoadingActive;

static inline void
BottomPutPixel(uint8 *fb, int x, int y, uint8 r, uint8 g, uint8 b)
{
	if((uint32)x >= BOTTOM_WIDTH || (uint32)y >= BOTTOM_HEIGHT)
		return;
	uint8 *pixel = fb + ((x * BOTTOM_HEIGHT) + (BOTTOM_HEIGHT - 1 - y)) * BOTTOM_BPP;
	pixel[0] = b; pixel[1] = g; pixel[2] = r;
}

static void
BottomFillRect(uint8 *fb, int left, int top, int right, int bottom,
	uint8 r, uint8 g, uint8 b)
{
	left = clamp(left, 0, BOTTOM_WIDTH); right = clamp(right, 0, BOTTOM_WIDTH);
	top = clamp(top, 0, BOTTOM_HEIGHT); bottom = clamp(bottom, 0, BOTTOM_HEIGHT);
	for(int x = left; x < right; x++)
		for(int y = top; y < bottom; y++)
			BottomPutPixel(fb, x, y, r, g, b);
}

static void
BottomDrawChar(uint8 *fb, int x, int y, unsigned char c, uint8 r, uint8 g, uint8 b)
{
	ConsoleFont *font = &consoleGetDefault()->font;
	if(c < font->asciiOffset || c >= font->asciiOffset + font->numChars)
		c = '?';
	const uint8 *glyph = font->gfx + (c - font->asciiOffset) * 8;
	for(int row = 0; row < 8; row++)
		for(int column = 0; column < 8; column++)
			if(glyph[row] & (0x80 >> column))
				BottomPutPixel(fb, x + column, y + row, r, g, b);
}

static void
BottomDrawText(uint8 *fb, int y, const char *text, uint8 r, uint8 g, uint8 b)
{
	if(text == nil) return;
	int length = Min((int)strlen(text), 38);
	int x = (BOTTOM_WIDTH - length * 8) / 2;
	for(int i = 0; i < length; i++)
		BottomDrawChar(fb, x + i * 8, y, (unsigned char)text[i], r, g, b);
}

static void
DrawBottomLoadingScreen(const char *detail, float progress)
{
	uint8 *fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, nil, nil);
	if(fb == nil) return;
	bool drawProgress = progress >= 0.0f;
	progress = clamp(progress, 0.0f, 1.0f);
	BottomFillRect(fb, 0, 0, BOTTOM_WIDTH, BOTTOM_HEIGHT, 0, 0, 0);
	BottomFillRect(fb, 0, 0, 320, 3, 55, 110, 210);
	BottomFillRect(fb, 0, 237, 320, 240, 16, 36, 78);
	// GTA3 already owns the upper-screen progress bar.  Keep the lower screen
	// to the same stage/percentage composition as reVC, centred as one block.
	BottomDrawText(fb, drawProgress ? 101 : 113,
		detail ? detail : "Please wait", 205, 220, 245);
	if(drawProgress){
		char percent[16];
		sprintf(percent, "%3d%%", (int)(progress * 100.0f + 0.5f));
		BottomDrawText(fb, 125, percent, 130, 180, 255);
	}
	GSPGPU_FlushDataCache(fb, BOTTOM_WIDTH * BOTTOM_HEIGHT * BOTTOM_BPP);
	// Direct framebuffer writes target the hidden lower backbuffer.  Present it
	// explicitly; the top RenderWare camera only owns the upper LCD swap here.
	gfxScreenSwapBuffers(GFX_BOTTOM, false);
}

static void
ClearBottomLoadingScreen(void)
{
	BottomLoadingActive = false;
	for(int i = 0; i < 2; i++) {
		uint8 *fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, nil, nil);
		if(fb) {
			memset(fb, 0, BOTTOM_WIDTH * BOTTOM_HEIGHT * BOTTOM_BPP);
			GSPGPU_FlushDataCache(fb, BOTTOM_WIDTH * BOTTOM_HEIGHT * BOTTOM_BPP);
		}
		gfxScreenSwapBuffers(GFX_BOTTOM, false);
	}
}
}
#endif

#if defined(_3DS) && defined(ENABLE_3DS_BOTTOM_RADAR)
namespace {
static RwCamera *BottomRadarCamera;
static RwTexture *BottomMenuMapTexture;
enum { NUM_BOTTOM_TOUCH_PARTS = 4 };
static RwTexture *BottomTouchTextures[NUM_BOTTOM_TOUCH_PARTS];

struct BottomTouchPart {
	int sourceX, sourceY, width, height, textureWidth, textureHeight;
};

static const BottomTouchPart BottomTouchParts[NUM_BOTTOM_TOUCH_PARTS] = {
	{ 23, 26, 125, 79, 128, 128 }, { 173, 26, 125, 79, 128, 128 },
	{ 28, 120, 256, 102, 256, 128 }, { 284, 120, 8, 102, 8, 128 }
};

static RwCamera *
CreateBottomRadarCamera(void)
{
	RwCamera *camera = RwCameraCreate();
	if(camera == nil) return nil;
	RwFrame *frame = RwFrameCreate();
	RwRaster *raster = RwRasterCreate(320, 240, 0, rwRASTERTYPECAMERA);
	RwRaster *zRaster = RwRasterCreate(320, 240, 0, rwRASTERTYPEZBUFFER);
	if(frame == nil || raster == nil || zRaster == nil) {
		if(frame) RwFrameDestroy(frame);
		if(raster) RwRasterDestroy(raster);
		if(zRaster) RwRasterDestroy(zRaster);
		RwCameraDestroy(camera);
		return nil;
	}
	RwCameraSetFrame(camera, frame);
	RwCameraSetRaster(camera, raster);
	RwCameraSetZRaster(camera, zRaster);
	RwCameraSetNearClipPlane(camera, 0.1f);
	RwCameraSetFarClipPlane(camera, 100.0f);
	RwV2d viewWindow = { 0.7f, 0.525f };
	RwCameraSetViewWindow(camera, &viewWindow);
	return camera;
}

static void
DrawBottomFrontEndWorldMap(void)
{
	// Liberty City's map artwork is built for a black surround.  Keep the GTA3
	// visual identity here; dark blue belongs only to the status rail and trim.
	CSprite2d::DrawRect(CRect(0.0f, 0.0f, 320.0f, 240.0f), CRGBA(0, 0, 0, 255));
	if(BottomMenuMapTexture) {
		CSprite2d map;
		map.m_pTexture = BottomMenuMapTexture;
		// Scale the 320x220 artwork uniformly about the LCD centre.  A 12/11
		// zoom makes its original 10-pixel top margin reach the upper edge
		// without shifting the composition off-centre.
		map.Draw(CRect(-14.55f, 0.0f, 334.55f, 240.0f),
			CRGBA(255, 255, 255, 255),
			0.0f, 0.0f, 320.0f / 512.0f, 0.0f,
			0.0f, 220.0f / 256.0f, 320.0f / 512.0f, 220.0f / 256.0f);
		map.m_pTexture = nil;
		// The cropped source has one detached fragment at its upper edge.  Mask
		// only that fragment while allowing the main island to touch the LCD edge.
		CSprite2d::DrawRect(CRect(180.0f, 0.0f, 224.0f, 8.0f), CRGBA(0, 0, 0, 255));
		return;
	}
#ifdef MENU_MAP
	if(!FrontEndMenuManager.m_bSpritesLoaded)
		return;
	const int sprites[9] = {
		MAPTOP1, MAPTOP2, MAPTOP3, MAPMID1, MAPMID2, MAPMID3,
		MAPBOT1, MAPBOT2, MAPBOT3
	};
	for(int row = 0; row < 3; row++)
		for(int column = 0; column < 3; column++) {
			CSprite2d &sprite = CMenuManager::m_aMapSprites[sprites[row * 3 + column]];
			if(sprite.m_pTexture)
				sprite.Draw(CRect(40.0f + column * 80.0f, row * 80.0f,
					120.0f + column * 80.0f, 80.0f + row * 80.0f),
					CRGBA(255, 255, 255, 255));
		}
#endif
}

static RwTexture *
CreateEmbeddedTexturePart(const uint8 *rgba, int sourceWidth, const BottomTouchPart &part)
{
	RwImage *image = RwImageCreate(part.textureWidth, part.textureHeight, 32);
	if(image == nil || RwImageAllocatePixels(image) == nil) {
		if(image) RwImageDestroy(image);
		return nil;
	}
	uint8 *pixels = RwImageGetPixels(image);
	int32 stride = RwImageGetStride(image);
	memset(pixels, 0, stride * part.textureHeight);
	for(int y = 0; y < part.height; y++)
		memcpy(pixels + y * stride,
			rgba + ((part.sourceY + y) * sourceWidth + part.sourceX) * 4,
			part.width * 4);
	int32 width, height, depth, format;
	RwImageFindRasterFormat(image, rwRASTERTYPETEXTURE, &width, &height, &depth, &format);
	RwRaster *raster = RwRasterCreate(width, height, depth, format);
	if(raster == nil || RwRasterSetFromImage(raster, image) == nil) {
		if(raster) RwRasterDestroy(raster);
		RwImageDestroy(image);
		return nil;
	}
	RwImageDestroy(image);
	return RwTextureCreate(raster);
}

static bool
CreateBottomTouchTextures(void)
{
	unsigned char *rgba = nil;
	unsigned width = 0, height = 0;
	if(lodepng_decode32(&rgba, &width, &height,
		bottom_touch_bin, bottom_touch_bin_size) != 0 || width != 320 || height != 240) {
		free(rgba);
		return false;
	}
	for(int i = 0; i < NUM_BOTTOM_TOUCH_PARTS; i++) {
		BottomTouchTextures[i] = CreateEmbeddedTexturePart(rgba, 320, BottomTouchParts[i]);
		if(BottomTouchTextures[i] == nil) {
			free(rgba);
			return false;
		}
	}
	free(rgba);
	return true;
}

static bool
CreateBottomMenuMapTexture(void)
{
	unsigned char *rgba = nil;
	unsigned width = 0, height = 0;
	if(lodepng_decode32(&rgba, &width, &height,
		bottom_menu_map_bin, bottom_menu_map_bin_size) != 0 ||
		width != 320 || height != 220) {
		free(rgba);
		return false;
	}
	const BottomTouchPart part = { 0, 0, 320, 220, 512, 256 };
	BottomMenuMapTexture = CreateEmbeddedTexturePart(rgba, 320, part);
	free(rgba);
	return BottomMenuMapTexture != nil;
}

static void
DrawBottomHud(CPlayerPed *player)
{
	if(player == nil)
		return;

	const float hudYOffset = 10.0f;
	char ascii[32];
	wchar text[32];
	CWeapon *weapon = player->GetWeapon();
	CFont::SetBackgroundOff();
	CFont::SetJustifyOff();
	CFont::SetRightJustifyOff();
	CFont::SetCentreOn();
	CFont::SetCentreSize(78.0f);
	CFont::SetPropOn();
	CFont::SetDropShadowPosition(2);
	CFont::SetDropColor(CRGBA(0, 0, 0, 255));
	CFont::SetScale(0.46f, 0.70f);
	CFont::SetFontStyle(FONT_BANK);
	if(weapon->m_eWeaponType != WEAPONTYPE_UNARMED &&
		weapon->m_eWeaponType != WEAPONTYPE_BASEBALLBAT) {
		sprintf(ascii, "%d-%d", Min(weapon->m_nAmmoTotal - weapon->m_nAmmoInClip, 9999),
			weapon->m_nAmmoInClip);
		AsciiToUnicode(ascii, text);
		CFont::SetColor(CRGBA(130, 180, 255, 255));
		CFont::PrintString(280.0f, 64.0f + hudYOffset, text);
	}
	CFont::SetPropOff();
	CFont::SetFontStyle(FONT_HEADING);
	CFont::SetScale(0.52f, 0.78f);
	sprintf(ascii, "%02d:%02d", CClock::GetHours(), CClock::GetMinutes());
	AsciiToUnicode(ascii, text);
	CFont::SetColor(CRGBA(194, 165, 120, 255)); // stock GTA3 clock
	CFont::PrintString(280.0f, 96.0f + hudYOffset, text);
	sprintf(ascii, "$%07d", CWorld::Players[CWorld::PlayerInFocus].m_nVisibleMoney);
	AsciiToUnicode(ascii, text);
	CFont::SetScale(0.44f, 0.67f);
	CFont::SetColor(CRGBA(89, 115, 150, 255)); // stock GTA3 money
	CFont::PrintString(280.0f, 128.0f + hudYOffset, text);
	sprintf(ascii, "%03d", (int)(player->m_fHealth + 0.5f));
	AsciiToUnicode(ascii, text);
	CFont::SetScale(0.52f, 0.78f);
	CFont::SetColor(CRGBA(186, 101, 50, 255)); // stock GTA3 health
	CFont::PrintString(288.0f, 160.0f + hudYOffset, text);
	AsciiToUnicode("{", text);
	CFont::SetScale(0.48f, 0.74f);
	CFont::PrintString(258.0f, 160.0f + hudYOffset, text);
	sprintf(ascii, "%03d", (int)(player->m_fArmour + 0.5f));
	AsciiToUnicode(ascii, text);
	CFont::SetScale(0.52f, 0.78f);
	CFont::SetColor(CRGBA(124, 140, 95, 255)); // stock GTA3 armour
	CFont::PrintString(288.0f, 192.0f + hudYOffset, text);
	AsciiToUnicode("[", text);
	CFont::SetScale(0.48f, 0.74f);
	CFont::PrintString(258.0f, 192.0f + hudYOffset, text);
	CFont::DrawFonts();
}

static void
DrawBottomTouchOverlay(void)
{
	if(!CPad::Is3DSTouchOverlayVisible())
		return;
	for(int i = 0; i < NUM_BOTTOM_TOUCH_PARTS; i++) {
		if(BottomTouchTextures[i] == nil) continue;
		const BottomTouchPart &part = BottomTouchParts[i];
		CSprite2d overlay;
		overlay.m_pTexture = BottomTouchTextures[i];
		float maxU = (float)part.width / part.textureWidth;
		float maxV = (float)part.height / part.textureHeight;
		overlay.Draw(CRect((float)part.sourceX, (float)part.sourceY,
			(float)(part.sourceX + part.width), (float)(part.sourceY + part.height)),
			CRGBA(255, 255, 255, 255), 0.0f, 0.0f, maxU, 0.0f,
			0.0f, maxV, maxU, maxV);
		overlay.m_pTexture = nil;
	}
}

static void
RenderBottomRadar(void)
{
#ifdef ENABLE_3DS_BOTTOM_LOADING
	if(BottomLoadingActive) return;
#endif
	const bool coldStart = FrontEndMenuManager.m_bGameNotLoaded &&
		FrontEndMenuManager.m_bMenuActive;
	if(!coldStart && (FrontEndMenuManager.m_bGameNotLoaded || FrontEndMenuManager.m_bMenuActive))
		return; // Pause preserves the most recent lower-screen frame.
	if(BottomRadarCamera == nil)
		BottomRadarCamera = CreateBottomRadarCamera();
	if(BottomRadarCamera == nil) return;
	const bool gameplay = !coldStart && !TheCamera.m_WideScreenOn &&
		!CCutsceneMgr::IsCutsceneProcessing() && CHud::m_Wants_To_Draw_Hud &&
		FindPlayerPed() != nil;
	CRGBA clear(0, 0, 0, 255);
	RwCameraClear(BottomRadarCamera, &clear.rwRGBA,
		rwCAMERACLEARIMAGE | rwCAMERACLEARZ | rwCAMERACLEARSTENCIL);
	if(coldStart) {
		if(RwCameraBeginUpdate(BottomRadarCamera)) {
			CSprite2d::InitPerFrame();
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
			RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
			DrawBottomFrontEndWorldMap();
			RwCameraEndUpdate(BottomRadarCamera);
		}
		RwCameraShowRaster(BottomRadarCamera, nil, rwRASTERFLIPDONTWAIT);
		return;
	}
	if(!gameplay) {
		RwCameraShowRaster(BottomRadarCamera, nil, rwRASTERFLIPDONTWAIT);
		return;
	}
	CPlayerPed *player = FindPlayerPed();
	if(!RwCameraBeginUpdate(BottomRadarCamera)) return;
	CSprite2d::InitPerFrame();
	CRadar::m_bDrawingBottomScreen = true;
	CRadar::DrawMap();
	CRadar::DrawBlips();
	CRadar::m_bDrawingBottomScreen = false;
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	CSprite2d::DrawRect(CRect(240.0f, 0.0f, 320.0f, 240.0f), CRGBA(5, 12, 28, 165));
	CSprite2d::DrawRect(CRect(240.0f, 0.0f, 242.0f, 240.0f), CRGBA(55, 110, 210, 220));
	int weaponType = player->GetWeapon()->m_eWeaponType;
	if(weaponType >= 0 && weaponType < NUM_HUD_SPRITES)
		CHud::Sprites[weaponType].Draw(CRect(250.0f, 13.0f, 310.0f, 71.0f),
			CRGBA(255, 255, 255, 255));
	DrawBottomHud(player);
	DrawBottomTouchOverlay();
	RwCameraEndUpdate(BottomRadarCamera);
	RwCameraShowRaster(BottomRadarCamera, nil, rwRASTERFLIPDONTWAIT);
}
}

void
ShutdownBottomRadar(void)
{
	if(BottomRadarCamera) {
		CameraDestroy(BottomRadarCamera);
		BottomRadarCamera = nil;
	}
	for(int i = 0; i < NUM_BOTTOM_TOUCH_PARTS; i++) {
		if(BottomTouchTextures[i]) {
			RwTextureDestroy(BottomTouchTextures[i]);
			BottomTouchTextures[i] = nil;
		}
	}
	if(BottomMenuMapTexture) {
		RwTextureDestroy(BottomMenuMapTexture);
		BottomMenuMapTexture = nil;
	}
}
#endif

bool g_SlowMode = false;
char version_name[64];


void GameInit(void);
void SystemInit(void);
void TheGame(void);

#ifdef DEBUGMENU
void DebugMenuPopulate(void);
#endif

#ifndef FINAL
bool gbPrintMemoryUsage;
#endif

#ifdef PS2_MENU
#define WANT_TO_LOAD TheMemoryCard.m_bWantToLoad
#define FOUND_GAME_TO_LOAD TheMemoryCard.b_FoundRecentSavedGameWantToLoad
#else
#define WANT_TO_LOAD FrontEndMenuManager.m_bWantToLoad
#define FOUND_GAME_TO_LOAD b_FoundRecentSavedGameWantToLoad
#endif

#ifdef NEW_RENDERER
bool gbNewRenderer;
#define CLEARMODE (rwCAMERACLEARZ | rwCAMERACLEARSTENCIL)
#else
#define CLEARMODE (rwCAMERACLEARZ)
#endif

#ifdef __MWERKS__
void
debug(char *fmt, ...)
{
#ifndef MASTER
	// TODO put something here
#endif
}

void
Error(char *fmt, ...)
{
#ifndef MASTER
	// TODO put something here
#endif
}
#endif

void
ValidateVersion()
{
	int32 file = CFileMgr::OpenFile("models\\coll\\peds.col", "rb");
	char buff[128];

	if ( file != -1 )
	{
		CFileMgr::Seek(file, 100, SEEK_SET);
		
		for ( int i = 0; i < 128; i++ )
		{
			CFileMgr::Read(file, &buff[i], sizeof(char));
			buff[i] -= 23;
			if ( buff[i] == '\0' )
				break;
			CFileMgr::Seek(file, 99, SEEK_CUR);
		}
		
		if ( !strncmp(buff, "grandtheftauto3", 15) )
		{
			strncpy(version_name, &buff[15], 64);
			CFileMgr::CloseFile(file);
			return;
		}
	}

	LoadingScreen("Invalid version", NULL, NULL);
	
	while(true)
	{
		;
	}
}

bool
DoRWStuffStartOfFrame(int16 TopRed, int16 TopGreen, int16 TopBlue, int16 BottomRed, int16 BottomGreen, int16 BottomBlue, int16 Alpha)
{
	CRGBA TopColor(TopRed, TopGreen, TopBlue, Alpha);
	CRGBA BottomColor(BottomRed, BottomGreen, BottomBlue, Alpha);

#ifndef ASPECT_RATIO_SCALE
	CameraSize(Scene.camera, nil, SCREEN_VIEWWINDOW, (CMenuManager::m_PrefsUseWideScreen ? 16.f / 9.f : 4.f / 3.f));
#else
	CameraSize(Scene.camera, nil, SCREEN_VIEWWINDOW, SCREEN_ASPECT_RATIO);
#endif
	CVisibilityPlugins::SetRenderWareCamera(Scene.camera);
	RwCameraClear(Scene.camera, &TopColor.rwRGBA, CLEARMODE);

	if(!RsCameraBeginUpdate(Scene.camera))
		return false;

#ifdef FIX_BUGS
	CSprite2d::SetRecipNearClip();
#endif
	CSprite2d::InitPerFrame();

	if(Alpha != 0)
		CSprite2d::DrawRect(CRect(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT), BottomColor, BottomColor, TopColor, TopColor);

	return true;
}

bool
DoRWStuffStartOfFrame_Horizon(int16 TopRed, int16 TopGreen, int16 TopBlue, int16 BottomRed, int16 BottomGreen, int16 BottomBlue, int16 Alpha)
{
#ifndef ASPECT_RATIO_SCALE
	CameraSize(Scene.camera, nil, SCREEN_VIEWWINDOW, (CMenuManager::m_PrefsUseWideScreen ? 16.f/9.f : 4.f/3.f));
#else
	CameraSize(Scene.camera, nil, SCREEN_VIEWWINDOW, SCREEN_ASPECT_RATIO);
#endif
	CVisibilityPlugins::SetRenderWareCamera(Scene.camera);
	RwCameraClear(Scene.camera, &gColourTop, CLEARMODE);

	if(!RsCameraBeginUpdate(Scene.camera))
		return false;

	TheCamera.m_viewMatrix.Update();
	CClouds::RenderBackground(TopRed, TopGreen, TopBlue, BottomRed, BottomGreen, BottomBlue, Alpha);

	return true;
}

// This is certainly a very useful function
void
DoRWRenderHorizon(void)
{
	CClouds::RenderHorizon();
}

void
DoFade(void)
{
	if(CTimer::GetIsPaused())
		return;

#ifdef PS2_MENU
	if(TheMemoryCard.JustLoadedDontFadeInYet){
		TheMemoryCard.JustLoadedDontFadeInYet = false;
		TheMemoryCard.TimeStartedCountingForFade = CTimer::GetTimeInMilliseconds();
	}
#else
	if(JustLoadedDontFadeInYet){
		JustLoadedDontFadeInYet = false;
		TimeStartedCountingForFade = CTimer::GetTimeInMilliseconds();
	}
#endif

#ifdef PS2_MENU
	if(TheMemoryCard.StillToFadeOut){
		if(CTimer::GetTimeInMilliseconds() - TheMemoryCard.TimeStartedCountingForFade > TheMemoryCard.TimeToStayFadedBeforeFadeOut){
			TheMemoryCard.StillToFadeOut = false;
#else
	if(StillToFadeOut){
		if(CTimer::GetTimeInMilliseconds() - TimeStartedCountingForFade > TimeToStayFadedBeforeFadeOut){
			StillToFadeOut = false;
#endif
			TheCamera.Fade(3.0f, FADE_IN);
			TheCamera.ProcessFade();
			TheCamera.ProcessMusicFade();
		}else{
			TheCamera.SetFadeColour(0, 0, 0);
			TheCamera.Fade(0.0f, FADE_OUT);
			TheCamera.ProcessFade();
		}
	}

	if(CDraw::FadeValue != 0 || CMenuManager::m_PrefsBrightness < 256){
		CSprite2d *splash = LoadSplash(nil);

		CRGBA fadeColor;
		CRect rect;
		int fadeValue = CDraw::FadeValue;
		float brightness = Min(CMenuManager::m_PrefsBrightness, 256);
		if(brightness <= 50)
			brightness = 50;
		if(FrontEndMenuManager.m_bMenuActive)
			brightness = 256;

		if(TheCamera.m_FadeTargetIsSplashScreen)
			fadeValue = 0;

		float fade = fadeValue + 256 - brightness;
		if(fade == 0){
			fadeColor.r = 0;
			fadeColor.g = 0;
			fadeColor.b = 0;
			fadeColor.a = 0;
		}else{
			fadeColor.r = fadeValue * CDraw::FadeRed / fade;
			fadeColor.g = fadeValue * CDraw::FadeGreen / fade;
			fadeColor.b = fadeValue * CDraw::FadeBlue / fade;
			int alpha = 255 - brightness*(256 - fadeValue)/256;
			if(alpha < 0)
				alpha = 0;
			fadeColor.a = alpha;
		}

		if(TheCamera.m_WideScreenOn
#ifdef CUTSCENE_BORDERS_SWITCH
			&& CMenuManager::m_PrefsCutsceneBorders
#endif
			){
			// what's this?
			float y = SCREEN_HEIGHT/2 * TheCamera.m_ScreenReductionPercentage/100.0f;
			rect.left = 0.0f;
			rect.right = SCREEN_WIDTH;
#ifdef FIX_BUGS
			rect.top = y - SCREEN_SCALE_Y(8.0f);
			rect.bottom = SCREEN_HEIGHT - y - SCREEN_SCALE_Y(8.0f);
#else
			rect.top = y - 8.0f;
			rect.bottom = SCREEN_HEIGHT - y - 8.0f;
#endif // FIX_BUGS
		}else{
			rect.left = 0.0f;
			rect.right = SCREEN_WIDTH;
			rect.top = 0.0f;
			rect.bottom = SCREEN_HEIGHT;
		}
		CSprite2d::DrawRect(rect, fadeColor);

		if(CDraw::FadeValue != 0 && TheCamera.m_FadeTargetIsSplashScreen){
			fadeColor.r = 255;
			fadeColor.g = 255;
			fadeColor.b = 255;
			fadeColor.a = CDraw::FadeValue;
			splash->Draw(CRect(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT), fadeColor, fadeColor, fadeColor, fadeColor);
		}
	}
}

bool
RwGrabScreen(RwCamera *camera, RwChar *filename)
{
	char temp[255];
	RwImage *pImage = RsGrabScreen(camera);
	bool result = true;

	if (pImage == nil)
		return false;

	strcpy(temp, CFileMgr::GetRootDirName());
	strcat(temp, filename);

#ifndef LIBRW
	if (RtBMPImageWrite(pImage, &temp[0]) == nil)
#else
	if (RtPNGImageWrite(pImage, &temp[0]) == nil)
#endif
		result = false;
	RwImageDestroy(pImage);
	return result;
}

#define TILE_WIDTH 576
#define TILE_HEIGHT 432

void
DoRWStuffEndOfFrame(void)
{
	CDebug::DisplayScreenStrings();	// custom
	CDebug::DebugDisplayTextBuffer();
	FlushObrsPrintfs();
	RwCameraEndUpdate(Scene.camera);
#ifdef ENABLE_3DS_BOTTOM_RADAR
	RenderBottomRadar();
#endif
	RsCameraShowRaster(Scene.camera);
#ifndef MASTER
	char s[48];
#ifdef THIS_IS_STUPID
	if (CPad::GetPad(1)->GetLeftShockJustDown()) {
		// try using both controllers for this thing... crazy bastards
		if (CPad::GetPad(0)->GetRightStickY() > 0) {
			sprintf(s, "screen%d%d.ras", CClock::ms_nGameClockHours, CClock::ms_nGameClockMinutes);
			// TODO
			//RtTileRender(Scene.camera, TILE_WIDTH * 2, TILE_HEIGHT * 2, TILE_WIDTH, TILE_HEIGHT, &NewTileRendererCB, nil, s);
		} else {
			sprintf(s, "screen%d%d.bmp", CClock::ms_nGameClockHours, CClock::ms_nGameClockMinutes);
			RwGrabScreen(Scene.camera, s);
		}
	}
#else
	if (CPad::GetPad(1)->GetLeftShockJustDown() || CPad::GetPad(0)->GetFJustDown(11)) {
		sprintf(s, "screen_%11lld.png", time(nil));
		RwGrabScreen(Scene.camera, s);
	}
#endif
#endif // !MASTER
}

static RwBool 
PluginAttach(void)
{
	if( !RpWorldPluginAttach() )
	{
		printf("Couldn't attach world plugin\n");
		
		return FALSE;
	}
	
	if( !RpSkinPluginAttach() )
	{
		printf("Couldn't attach RpSkin plugin\n");
		
		return FALSE;
	}
	
	if( !RpHAnimPluginAttach() )
	{
		printf("Couldn't attach RpHAnim plugin\n");
		
		return FALSE;
	}
	
	if( !NodeNamePluginAttach() )
	{
		printf("Couldn't attach node name plugin\n");
		
		return FALSE;
	}
	
	if( !CVisibilityPlugins::PluginAttach() )
	{
		printf("Couldn't attach visibility plugins\n");
		
		return FALSE;
	}
	
	if( !RpAnimBlendPluginAttach() )
	{
		printf("Couldn't attach RpAnimBlend plugin\n");
		
		return FALSE;
	}
	
	if( !RpMatFXPluginAttach() )
	{
		printf("Couldn't attach RpMatFX plugin\n");
		
		return FALSE;
	}
#ifdef ANISOTROPIC_FILTERING
	RpAnisotPluginAttach();
#endif
#ifdef EXTENDED_PIPELINES
	CustomPipes::CustomPipeRegister();
#endif

	return TRUE;
}

#ifdef GTA_PS2
#define NUM_PREALLOC_ATOMICS 3245
#define NUM_PREALLOC_CLUMPS 101
#define NUM_PREALLOC_FRAMES 2821
#define NUM_PREALLOC_GEOMETRIES 1404
#define NUM_PREALLOC_TEXDICTS 106
#define NUM_PREALLOC_TEXTURES 1900
#define NUM_PREALLOC_MATERIALS 3300
bool preAlloc;

void
PreAllocateRwObjects(void)
{
	int i;
	void **tmp = new void*[0x8000];
	preAlloc = true;

	for(i = 0; i < NUM_PREALLOC_ATOMICS; i++)
		tmp[i] = RpAtomicCreate();
	for(i = 0; i < NUM_PREALLOC_ATOMICS; i++)
		RpAtomicDestroy((RpAtomic*)tmp[i]);

	for(i = 0; i < NUM_PREALLOC_CLUMPS; i++)
		tmp[i] = RpClumpCreate();
	for(i = 0; i < NUM_PREALLOC_CLUMPS; i++)
		RpClumpDestroy((RpClump*)tmp[i]);

	for(i = 0; i < NUM_PREALLOC_FRAMES; i++)
		tmp[i] = RwFrameCreate();
	for(i = 0; i < NUM_PREALLOC_FRAMES; i++)
		RwFrameDestroy((RwFrame*)tmp[i]);

	for(i = 0; i < NUM_PREALLOC_GEOMETRIES; i++)
		tmp[i] = RpGeometryCreate(0, 0, 0);
	for(i = 0; i < NUM_PREALLOC_GEOMETRIES; i++)
		RpGeometryDestroy((RpGeometry*)tmp[i]);

	for(i = 0; i < NUM_PREALLOC_TEXDICTS; i++)
		tmp[i] = RwTexDictionaryCreate();
	for(i = 0; i < NUM_PREALLOC_TEXDICTS; i++)
		RwTexDictionaryDestroy((RwTexDictionary*)tmp[i]);

	for(i = 0; i < NUM_PREALLOC_TEXTURES; i++)
		tmp[i] = RwTextureCreate(RwRasterCreate(0, 0, 0, 0));
	for(i = 0; i < NUM_PREALLOC_TEXDICTS; i++)
		RwTextureDestroy((RwTexture*)tmp[i]);

	for(i = 0; i < NUM_PREALLOC_MATERIALS; i++)
		tmp[i] = RpMaterialCreate();
	for(i = 0; i < NUM_PREALLOC_MATERIALS; i++)
		RpMaterialDestroy((RpMaterial*)tmp[i]);

	delete[] tmp;
	preAlloc = false;
}
#endif

static RwBool 
Initialise3D(void *param)
{
	if (RsRwInitialize(param))
	{
#ifdef DEBUGMENU
		DebugMenuInit();
		DebugMenuPopulate();
#endif // !DEBUGMENU
		if(!CGame::InitialiseRenderWare())
			return FALSE;
#ifdef ENABLE_3DS_BOTTOM_RADAR
		// re3 creates its texture/HUD pools inside InitialiseRenderWare. Creating
		// an RwRaster before those pools exist crashes during boot on hardware.
		// This is still early enough to reserve the tiny overlay before world TXDs.
		CreateBottomTouchTextures();
		CreateBottomMenuMapTexture();
#endif
		return TRUE;
	}

	return (FALSE);
}

static void 
Terminate3D(void)
{
	CGame::ShutdownRenderWare();
#ifdef DEBUGMENU
	DebugMenuShutdown();
#endif // !DEBUGMENU
	
	RsRwTerminate();

	return;
}

CSprite2d splash;
int splashTxdId = -1;

CSprite2d*
LoadSplash(const char *name)
{
	RwTexDictionary *txd;
	char filename[140];
	RwTexture *tex = nil;

	if(name == nil)
		return &splash;
	if(splashTxdId == -1)
		splashTxdId = CTxdStore::AddTxdSlot("splash");

	txd = CTxdStore::GetSlot(splashTxdId)->texDict;
	if(txd)
		tex = RwTexDictionaryFindNamedTexture(txd, name);
	// if texture is found, splash was already set up below

	if(tex == nil){
		CFileMgr::SetDir("TXD\\");
		sprintf(filename, "%s.txd", name);
		if(splash.m_pTexture)
			splash.Delete();
		if(txd)
			CTxdStore::RemoveTxd(splashTxdId);
		CTxdStore::LoadTxd(splashTxdId, filename);
		CTxdStore::AddRef(splashTxdId);
		CTxdStore::PushCurrentTxd();
		CTxdStore::SetCurrentTxd(splashTxdId);
		splash.SetTexture(name);
		CTxdStore::PopCurrentTxd();
		CFileMgr::SetDir("");
	}

	return &splash;
}

void
DestroySplashScreen(void)
{
	splash.Delete();
	if(splashTxdId != -1)
		CTxdStore::RemoveTxdSlot(splashTxdId);
	splashTxdId = -1;
}

Const char*
GetRandomSplashScreen(void)
{
	int index;
	static int index2 = 0;
	static char splashName[128];
	static int splashIndex[24] = {
		25, 22, 4, 13,
		1, 21, 14, 16,
		10, 12, 5, 9,
		11, 18, 3, 2,
		19, 23, 7, 17,
		15, 6, 8, 20
	};

	index = splashIndex[4*index2 + CGeneral::GetRandomNumberInRange(0, 3)];
	index2++;
	if(index2 == 6)
		index2 = 0;
	sprintf(splashName, "loadsc%d", index);
	return splashName;
}

Const char*
GetLevelSplashScreen(int level)
{
	static Const char *splashScreens[4] = {
		nil,
		"splash1",
		"splash2",
		"splash3",
	};

	return splashScreens[level];
}

void
ResetLoadingScreenBar()
{
	NumberOfChunksLoaded = 0.0f;
}

void
LoadingScreen(const char *str1, const char *str2, const char *splashscreen)
{
	CSprite2d *splash;
	bool loadingOnly = str1 != nil && str2 == nil && !strcmp(str1, "Loading");

#ifdef DISABLE_LOADING_SCREEN
	if (str1 && str2)
		return;
#endif

#ifndef RANDOMSPLASH
	if(CGame::frenchGame || CGame::germanGame || !CGame::nastyGame)
		splashscreen = "mainsc2";
	else
		splashscreen = "mainsc1";
#endif

	splash = LoadSplash(splashscreen);

#if defined(_3DS) && defined(ENABLE_3DS_BOTTOM_LOADING)
	if(BottomLoadingActive)
		DrawBottomLoadingScreen(loadingOnly ? str1 : str2,
			loadingOnly ? -1.0f : NumberOfChunksLoaded / TOTALNUMCHUNKS);
#endif

#ifndef GTA_PS2
	if(RsGlobal.quit)
		return;
#endif

#ifndef GTA_PS2
	if(DoRWStuffStartOfFrame(0, 0, 0, 0, 0, 0, 255))
#else
	DoRWStuffStartOfFrame(0, 0, 0, 0, 0, 0, 255);
#endif
	{
		CSprite2d::SetRecipNearClip();
		CSprite2d::InitPerFrame();
		CFont::InitPerFrame();
		DefinedState();
		RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSCLAMP);
		splash->Draw(CRect(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT), CRGBA(255, 255, 255, 255));

		if(str1){
			float hpos = SCREEN_SCALE_X(40);
			float length = SCREEN_WIDTH - SCREEN_SCALE_X(100);
			float vpos = SCREEN_HEIGHT - SCREEN_SCALE_Y(13);
			float height = SCREEN_SCALE_Y(7);
			if(!loadingOnly){
				NumberOfChunksLoaded += 1;
				CSprite2d::DrawRect(CRect(hpos, vpos, hpos + length, vpos + height), CRGBA(40, 53, 68, 255));

				length *= NumberOfChunksLoaded/TOTALNUMCHUNKS;
				CSprite2d::DrawRect(CRect(hpos, vpos, hpos + length, vpos + height), CRGBA(81, 106, 137, 255));
			}

			// this is done by the game but is unused
			CFont::SetScale(SCREEN_SCALE_X(2), SCREEN_SCALE_Y(2));
			CFont::SetPropOn();
			CFont::SetRightJustifyOn();
			CFont::SetFontStyle(FONT_HEADING);

#ifdef CHATTYSPLASH
			// my attempt
			static wchar tmpstr[80];
			float yscale = SCREEN_SCALE_Y(0.9f);
			vpos -= 45*yscale;
			CFont::SetScale(SCREEN_SCALE_X(0.75f), yscale);
			CFont::SetPropOn();
			CFont::SetRightJustifyOff();
			CFont::SetFontStyle(FONT_BANK);
			CFont::SetColor(CRGBA(255, 255, 255, 255));
			AsciiToUnicode(str1, tmpstr);
			CFont::PrintString(hpos, vpos, tmpstr);
			vpos += 22*yscale;
			if (str2) {
				AsciiToUnicode(str2, tmpstr);
				CFont::PrintString(hpos, vpos, tmpstr);
			}
#endif
		}

		CFont::DrawFonts();
 		DoRWStuffEndOfFrame();
	}
}

void
LoadingIslandScreen(const char *levelName)
{
	CSprite2d *splash;
	wchar *name;
	char str[100];
	wchar wstr[80];
	CRGBA col;

	splash = LoadSplash(nil);
	name = TheText.Get(levelName);
	
#ifndef GTA_PS2
	if(!DoRWStuffStartOfFrame(0, 0, 0, 0, 0, 0, 255))
		return;
#else
	DoRWStuffStartOfFrame(0, 0, 0, 0, 0, 0, 255);
#endif

	CSprite2d::SetRecipNearClip();
	CSprite2d::InitPerFrame();
	CFont::InitPerFrame();
	DefinedState();
	col = CRGBA(255, 255, 255, 255);
	splash->Draw(CRect(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT), col, col, col, col);
	CFont::SetBackgroundOff();
#ifdef FIX_BUGS
	CFont::SetScale(SCREEN_SCALE_X(1.5f), SCREEN_SCALE_Y(1.5f));
#else
	CFont::SetScale(1.5f, 1.5f);
#endif
	CFont::SetPropOn();
	CFont::SetRightJustifyOn();
#ifdef FIX_BUGS
	CFont::SetRightJustifyWrap(SCREEN_SCALE_X(150.0f));
#else
	CFont::SetRightJustifyWrap(150.0f);
#endif
	CFont::SetFontStyle(FONT_HEADING);
	sprintf(str, "WELCOME TO");
	AsciiToUnicode(str, wstr);
	CFont::SetDropColor(CRGBA(0, 0, 0, 255));
	CFont::SetDropShadowPosition(3);
	CFont::SetColor(CRGBA(243, 237, 71, 255));
#if !defined(PS2_HUD) && defined(GTA_PC)
	CFont::SetScale(SCREEN_SCALE_X(1.2f), SCREEN_SCALE_Y(1.2f));
#endif

#ifdef PS2_HUD
	#ifdef FIX_BUGS
	CFont::PrintString(SCREEN_SCALE_FROM_RIGHT(20.0f), SCREEN_SCALE_FROM_BOTTOM(140.0f), TheText.Get("WELCOME"));
	#else
	CFont::PrintString(SCREEN_WIDTH - 20, SCREEN_HEIGHT - 140, TheText.Get("WELCOME"));
	#endif
#else
	#ifdef FIX_BUGS
	CFont::PrintString(SCREEN_SCALE_FROM_RIGHT(20.0f), SCREEN_SCALE_FROM_BOTTOM(110.0f), TheText.Get("WELCOME"));
	#else
	CFont::PrintString(SCREEN_WIDTH - 20, SCREEN_SCALE_FROM_BOTTOM(110.0f), TheText.Get("WELCOME"));
	#endif
#endif
	TextCopy(wstr, name);
	TheText.UpperCase(wstr);
	CFont::SetColor(CRGBA(243, 237, 71, 255));
#if !defined(PS2_HUD) && defined(GTA_PC)
	CFont::SetScale(SCREEN_SCALE_X(1.2f), SCREEN_SCALE_Y(1.2f));
#endif

#ifdef PS2_HUD
	#ifdef FIX_BUGS
		CFont::PrintString(SCREEN_SCALE_FROM_RIGHT(20.0f), SCREEN_SCALE_FROM_BOTTOM(110.0f), wstr);
	#else
		CFont::PrintString(SCREEN_WIDTH-20, SCREEN_HEIGHT - 110, wstr);
	#endif
#else
	#ifdef FIX_BUGS
		CFont::PrintString(SCREEN_SCALE_FROM_RIGHT(20.0f), SCREEN_SCALE_FROM_BOTTOM(80.0f), wstr);
	#else
		CFont::PrintString(SCREEN_WIDTH-20, SCREEN_SCALE_FROM_BOTTOM(80.0f), wstr);
	#endif
#endif
	CFont::DrawFonts();
	DoRWStuffEndOfFrame();
}

void
ProcessSlowMode(void)
{  
	int16 lX = CPad::GetPad(0)->NewState.LeftStickX;
	int16 lY = CPad::GetPad(0)->NewState.LeftStickY;
	int16 rX = CPad::GetPad(0)->NewState.RightStickX;
	int16 rY = CPad::GetPad(0)->NewState.RightStickY;
	int16 L1 = CPad::GetPad(0)->NewState.LeftShoulder1;
	int16 L2 = CPad::GetPad(0)->NewState.LeftShoulder2;
	int16 R1 = CPad::GetPad(0)->NewState.RightShoulder1;
	int16 R2 = CPad::GetPad(0)->NewState.RightShoulder2;
	int16 up = CPad::GetPad(0)->NewState.DPadUp;
	int16 down = CPad::GetPad(0)->NewState.DPadDown;
	int16 left = CPad::GetPad(0)->NewState.DPadLeft;
	int16 right = CPad::GetPad(0)->NewState.DPadRight;
	int16 start = CPad::GetPad(0)->NewState.Start;
	int16 select = CPad::GetPad(0)->NewState.Select;
	int16 square = CPad::GetPad(0)->NewState.Square;
	int16 triangle = CPad::GetPad(0)->NewState.Triangle;
	int16 cross = CPad::GetPad(0)->NewState.Cross;
	int16 circle = CPad::GetPad(0)->NewState.Circle;
	int16 L3 = CPad::GetPad(0)->NewState.LeftShock;
	int16 R3 = CPad::GetPad(0)->NewState.RightShock;
	int16 networktalk = CPad::GetPad(0)->NewState.NetworkTalk;
	int16 stop = true;
	
	do
	{
		if ( CPad::GetPad(1)->GetLeftShoulder1JustDown() || CPad::GetPad(1)->GetRightShoulder1() )
			break;
		
		if ( stop )
		{
			CTimer::Stop();
			stop = false;
		}
		
		CPad::UpdatePads();
		
		RwCameraBeginUpdate(Scene.camera);
		RwCameraEndUpdate(Scene.camera);
		
		if ( CPad::GetPad(1)->GetLeftShoulder1JustDown() || CPad::GetPad(1)->GetRightShoulder1() )
			break;
	
	} while (!CPad::GetPad(1)->GetRightShoulder1());
	
	
	CPad::GetPad(0)->OldState.LeftStickX = lX;
	CPad::GetPad(0)->OldState.LeftStickY = lY;
	CPad::GetPad(0)->OldState.RightStickX = rX;
	CPad::GetPad(0)->OldState.RightStickY = rY;
	CPad::GetPad(0)->OldState.LeftShoulder1 = L1;
	CPad::GetPad(0)->OldState.LeftShoulder2 = L2;
	CPad::GetPad(0)->OldState.RightShoulder1 = R1;
	CPad::GetPad(0)->OldState.RightShoulder2 = R2;
	CPad::GetPad(0)->OldState.DPadUp = up;
	CPad::GetPad(0)->OldState.DPadDown = down;
	CPad::GetPad(0)->OldState.DPadLeft = left;
	CPad::GetPad(0)->OldState.DPadRight = right;
	CPad::GetPad(0)->OldState.Start = start;
	CPad::GetPad(0)->OldState.Select = select;
	CPad::GetPad(0)->OldState.Square = square;
	CPad::GetPad(0)->OldState.Triangle = triangle;
	CPad::GetPad(0)->OldState.Cross = cross;
	CPad::GetPad(0)->OldState.Circle = circle;
	CPad::GetPad(0)->OldState.LeftShock = L3;
	CPad::GetPad(0)->OldState.RightShock = R3;
	CPad::GetPad(0)->OldState.NetworkTalk = networktalk;
	CPad::GetPad(0)->NewState.LeftStickX = lX;
	CPad::GetPad(0)->NewState.LeftStickY = lY;
	CPad::GetPad(0)->NewState.RightStickX = rX;
	CPad::GetPad(0)->NewState.RightStickY = rY;
	CPad::GetPad(0)->NewState.LeftShoulder1 = L1;
	CPad::GetPad(0)->NewState.LeftShoulder2 = L2;
	CPad::GetPad(0)->NewState.RightShoulder1 = R1;
	CPad::GetPad(0)->NewState.RightShoulder2 = R2;
	CPad::GetPad(0)->NewState.DPadUp = up;
	CPad::GetPad(0)->NewState.DPadDown = down;
	CPad::GetPad(0)->NewState.DPadLeft = left;
	CPad::GetPad(0)->NewState.DPadRight = right;
	CPad::GetPad(0)->NewState.Start = start;
	CPad::GetPad(0)->NewState.Select = select;
	CPad::GetPad(0)->NewState.Square = square;
	CPad::GetPad(0)->NewState.Triangle = triangle;
	CPad::GetPad(0)->NewState.Cross = cross;
	CPad::GetPad(0)->NewState.Circle = circle;
	CPad::GetPad(0)->NewState.LeftShock = L3;
	CPad::GetPad(0)->NewState.RightShock = R3;
	CPad::GetPad(0)->NewState.NetworkTalk = networktalk;
}


float FramesPerSecondCounter;
int32 FrameSamples;

#ifndef MASTER
struct tZonePrint
{
  char name[12];
  CRect rect;
};

tZonePrint ZonePrint[] =
{
	{ "suburban", CRect(-1639.4f,  1014.3f, -226.23f, -1347.9f) },
	{ "comntop",  CRect(-223.52f,  203.62f,  616.79f, -413.6f)  },
	{ "comnbtm",  CRect(-227.24f, -413.6f,   620.51f, -911.84f) },
	{ "comse",    CRect( 200.35f, -911.84f,  620.51f, -1737.3f) },
	{ "comsw",    CRect(-223.52f, -911.84f,  200.35f, -1737.3f) },
	{ "industsw", CRect( 744.05f, -473.0f,   1067.5f, -1331.5f) },
	{ "industne", CRect( 1067.5f,  282.19f,  1915.3f, -473.0f)  },
	{ "industnw", CRect( 744.05f,  324.95f,  1067.5f, -473.0f)  },
	{ "industse", CRect( 1070.3f, -473.0f,   1918.1f, -1331.5f) },
	{ "no zone",  CRect( 0.0f,     0.0f,     0.0f,    0.0f)     }
};

void
PrintMemoryUsage(void)
{
// little hack
if(CPools::GetPtrNodePool() == nil)
return;

	// Style taken from LCS, modified for III
//	CFont::SetFontStyle(FONT_PAGER);
	CFont::SetFontStyle(FONT_BANK);
	CFont::SetBackgroundOff();
	CFont::SetWrapx(640.0f);
//	CFont::SetScale(0.5f, 0.75f);
	CFont::SetScale(0.4f, 0.75f);
	CFont::SetCentreOff();
	CFont::SetCentreSize(640.0f);
	CFont::SetJustifyOff();
	CFont::SetPropOn();
	CFont::SetColor(CRGBA(200, 200, 200, 200));
	CFont::SetBackGroundOnlyTextOff();
	CFont::SetDropShadowPosition(0);

	float y;

#ifdef USE_CUSTOM_ALLOCATOR
	y = 24.0f;
	sprintf(gString, "Total: %d blocks, %d bytes", gMainHeap.m_totalBlocksUsed, gMainHeap.m_totalMemUsed);
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(24.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Game: %d blocks, %d bytes", gMainHeap.GetBlocksUsed(MEMID_GAME), gMainHeap.GetMemoryUsed(MEMID_GAME));
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(24.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "World: %d blocks, %d bytes", gMainHeap.GetBlocksUsed(MEMID_WORLD), gMainHeap.GetMemoryUsed(MEMID_WORLD));
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(24.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Render: %d blocks, %d bytes", gMainHeap.GetBlocksUsed(MEMID_RENDER), gMainHeap.GetMemoryUsed(MEMID_RENDER));
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(24.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Render List: %d blocks, %d bytes", gMainHeap.GetBlocksUsed(MEMID_RENDERLIST), gMainHeap.GetMemoryUsed(MEMID_RENDERLIST));
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(24.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Default Models: %d blocks, %d bytes", gMainHeap.GetBlocksUsed(MEMID_DEF_MODELS), gMainHeap.GetMemoryUsed(MEMID_DEF_MODELS));
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(24.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Textures: %d blocks, %d bytes", gMainHeap.GetBlocksUsed(MEMID_TEXTURES), gMainHeap.GetMemoryUsed(MEMID_TEXTURES));
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(24.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Streaming: %d blocks, %d bytes", gMainHeap.GetBlocksUsed(MEMID_STREAM), gMainHeap.GetMemoryUsed(MEMID_STREAM));
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(24.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Streamed Models: %d blocks, %d bytes", gMainHeap.GetBlocksUsed(MEMID_STREAM_MODELS), gMainHeap.GetMemoryUsed(MEMID_STREAM_MODELS));
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(24.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Streamed Textures: %d blocks, %d bytes", gMainHeap.GetBlocksUsed(MEMID_STREAM_TEXUTRES), gMainHeap.GetMemoryUsed(MEMID_STREAM_TEXUTRES));
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(24.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Animation: %d blocks, %d bytes", gMainHeap.GetBlocksUsed(MEMID_ANIMATION), gMainHeap.GetMemoryUsed(MEMID_ANIMATION));
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(24.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Pools: %d blocks, %d bytes", gMainHeap.GetBlocksUsed(MEMID_POOLS), gMainHeap.GetMemoryUsed(MEMID_POOLS));
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(24.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Collision: %d blocks, %d bytes", gMainHeap.GetBlocksUsed(MEMID_COLLISION), gMainHeap.GetMemoryUsed(MEMID_COLLISION));
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(24.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Game Process: %d blocks, %d bytes", gMainHeap.GetBlocksUsed(MEMID_GAME_PROCESS), gMainHeap.GetMemoryUsed(MEMID_GAME_PROCESS));
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(24.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Script: %d blocks, %d bytes", gMainHeap.GetBlocksUsed(MEMID_SCRIPT), gMainHeap.GetMemoryUsed(MEMID_SCRIPT));
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(24.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Cars: %d blocks, %d bytes", gMainHeap.GetBlocksUsed(MEMID_CARS), gMainHeap.GetMemoryUsed(MEMID_CARS));
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(24.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Frontend: %d blocks, %d bytes", gMainHeap.GetBlocksUsed(MEMID_FRONTEND), gMainHeap.GetMemoryUsed(MEMID_FRONTEND));
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(24.0f, y, gUString);
	y += 12.0f;
#endif

	y = 132.0f;
	AsciiToUnicode("Pools usage:", gUString);
	CFont::PrintString(400.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "PtrNode: %d/%d", CPools::GetPtrNodePool()->GetNoOfUsedSpaces(), CPools::GetPtrNodePool()->GetSize());
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(400.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "EntryInfoNode: %d/%d", CPools::GetEntryInfoNodePool()->GetNoOfUsedSpaces(), CPools::GetEntryInfoNodePool()->GetSize());
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(400.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Ped: %d/%d", CPools::GetPedPool()->GetNoOfUsedSpaces(), CPools::GetPedPool()->GetSize());
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(400.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Vehicle: %d/%d", CPools::GetVehiclePool()->GetNoOfUsedSpaces(), CPools::GetVehiclePool()->GetSize());
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(400.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Building: %d/%d", CPools::GetBuildingPool()->GetNoOfUsedSpaces(), CPools::GetBuildingPool()->GetSize());
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(400.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Treadable: %d/%d", CPools::GetTreadablePool()->GetNoOfUsedSpaces(), CPools::GetTreadablePool()->GetSize());
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(400.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Object: %d/%d", CPools::GetObjectPool()->GetNoOfUsedSpaces(), CPools::GetObjectPool()->GetSize());
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(400.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Dummy: %d/%d", CPools::GetDummyPool()->GetNoOfUsedSpaces(), CPools::GetDummyPool()->GetSize());
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(400.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "AudioScriptObjects: %d/%d", CPools::GetAudioScriptObjectPool()->GetNoOfUsedSpaces(), CPools::GetAudioScriptObjectPool()->GetSize());
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(400.0f, y, gUString);
	y += 12.0f;
}

void
DisplayGameDebugText()
{
	static bool bDisplayPosn = false;
	static bool bDisplayRate = false;
#ifndef FINAL
	{
		SETTWEAKPATH("Debug");
		TWEAKBOOL(bDisplayPosn);
		TWEAKBOOL(bDisplayRate);
	}

	if(gbPrintMemoryUsage)
		PrintMemoryUsage();
#endif

	char str[200];
	wchar ustr[200];

#ifdef DRAW_GAME_VERSION_TEXT
	wchar ver[200];

	if(gDrawVersionText) // This realtime switch is our thing
	{

#ifdef USE_OUR_VERSIONING
	char verA[200];
	sprintf(verA,
#if defined _WIN32
			"Win "
#elif defined __linux__
		    "Linux "
#elif defined __APPLE__
		    "Mac OS X "
#elif defined __FreeBSD__
		    "FreeBSD "
#else
		    "Posix-compliant "
#endif
#if defined __LP64__ || defined _WIN64
			"64-bit "
#else
			"32-bit "
#endif
#if defined RW_D3D9
		    "D3D9 "
#elif defined RWLIBS
		    "D3D8 "
#elif defined RW_GL3
		    "OpenGL "
#endif
#if defined AUDIO_OAL
		    "OAL "
#elif defined AUDIO_MSS
		    "MSS "
#endif
#if defined _DEBUG || defined DEBUG
		    "DEBUG "
#endif
		    "%.8s",
		    g_GIT_SHA1);
	AsciiToUnicode(verA, ver);
	CFont::SetScale(SCREEN_SCALE_X(0.5f), SCREEN_SCALE_Y(0.7f));
#else
	AsciiToUnicode(version_name, ver);
	CFont::SetScale(SCREEN_SCALE_X(0.5f), SCREEN_SCALE_Y(0.5f));
#endif

	CFont::SetPropOn();
	CFont::SetBackgroundOff();
	CFont::SetFontStyle(FONT_BANK);
	CFont::SetCentreOff();
	CFont::SetRightJustifyOff();
	CFont::SetWrapx(SCREEN_WIDTH);
	CFont::SetJustifyOff();
	CFont::SetBackGroundOnlyTextOff();
	CFont::SetColor(CRGBA(255, 108, 0, 255));
#ifdef FIX_BUGS
	CFont::PrintString(SCREEN_SCALE_X(10.0f), SCREEN_SCALE_Y(10.0f), ver);
#else
	CFont::PrintString(10.0f, 10.0f, ver);
#endif
	}
#endif // #ifdef DRAW_GAME_VERSION_TEXT

	FrameSamples++;
#ifdef FIX_BUGS
	FramesPerSecondCounter += frameTime / 1000.f; // convert to seconds
	FramesPerSecond = FrameSamples / FramesPerSecondCounter;
#else
	FramesPerSecondCounter += 1000.0f / (CTimer::GetTimeStepNonClippedInSeconds() * 1000.0f);	
	FramesPerSecond = FramesPerSecondCounter / FrameSamples;
#endif
	
	if ( FrameSamples > 30 )
	{
		FramesPerSecondCounter = 0.0f;
		FrameSamples = 0;
	}
  
	if ( !TheCamera.WorldViewerBeingUsed 
		&& CPad::GetPad(1)->GetSquare() 
		&& CPad::GetPad(1)->GetTriangle()
		&& CPad::GetPad(1)->GetLeftShoulder2JustDown() )
	{
		bDisplayPosn = !bDisplayPosn;
	}

	if ( CPad::GetPad(1)->GetSquare()
		&& CPad::GetPad(1)->GetTriangle()
		&& CPad::GetPad(1)->GetRightShoulder2JustDown() )
	{
		bDisplayRate = !bDisplayRate;
	}
	
	if ( bDisplayPosn || bDisplayRate )
	{
		CVector pos = FindPlayerCoors();
		int32 ZoneId = ARRAY_SIZE(ZonePrint)-1; // no zone
		
		for ( int32 i = 0; i < ARRAY_SIZE(ZonePrint)-1; i++ )
		{
			if ( pos.x > ZonePrint[i].rect.left
				&& pos.x < ZonePrint[i].rect.right
				&& pos.y > ZonePrint[i].rect.bottom
				&& pos.y < ZonePrint[i].rect.top )
			{
				ZoneId = i;
			}
		}

		//NOTE: fps should be 30, but its 29 due to different fp2int conversion 
		if ( bDisplayRate )
			sprintf(str, "X:%5.1f, Y:%5.1f, Z:%5.1f, F-%d, %s", pos.x, pos.y, pos.z, (int32)FramesPerSecond, ZonePrint[ZoneId].name);
		else
			sprintf(str, "X:%5.1f, Y:%5.1f, Z:%5.1f, %s", pos.x, pos.y, pos.z, ZonePrint[ZoneId].name);
		
		AsciiToUnicode(str, ustr);
		
		CFont::SetPropOff();
		CFont::SetBackgroundOff();
#ifdef FIX_BUGS
		CFont::SetScale(SCREEN_SCALE_X(0.7f), SCREEN_SCALE_Y(1.5f));
#else
		CFont::SetScale(0.7f, 1.5f);
#endif
		CFont::SetCentreOff();
		CFont::SetRightJustifyOff();
		CFont::SetJustifyOff();
		CFont::SetBackGroundOnlyTextOff();
#ifdef FIX_BUGS
		CFont::SetWrapx(SCREEN_STRETCH_X(DEFAULT_SCREEN_WIDTH));
#else
		CFont::SetWrapx(DEFAULT_SCREEN_WIDTH);
#endif
		CFont::SetFontStyle(FONT_HEADING);
		
		CFont::SetColor(CRGBA(0, 0, 0, 255));
#ifdef FIX_BUGS
		CFont::PrintString(SCREEN_SCALE_X(40.0f+2.0f), SCREEN_SCALE_Y(40.0f+2.0f), ustr);
#else
		CFont::PrintString(40.0f+2.0f, 40.0f+2.0f, ustr);
#endif
		
		CFont::SetColor(CRGBA(255, 108, 0, 255));
#ifdef FIX_BUGS
		CFont::PrintString(SCREEN_SCALE_X(40.0f), SCREEN_SCALE_Y(40.0f), ustr);
#else
		CFont::PrintString(40.0f, 40.0f, ustr);
#endif
	}
}
#endif

#ifdef NEW_RENDERER
bool gbRenderRoads = true;
bool gbRenderEverythingBarRoads = true;
//bool gbRenderFadingInUnderwaterEntities = true;
bool gbRenderFadingInEntities = true;
bool gbRenderWater = true;
bool gbRenderBoats = true;
bool gbRenderVehicles = true;
bool gbRenderWorld0 = true;
bool gbRenderWorld1 = true;
bool gbRenderWorld2 = true;

void
MattRenderScene(void)
{
	// this calls CMattRenderer::Render
	/// CWorld::AdvanceCurrentScanCode();
	// CMattRenderer::ResetRenderStates
	/// CRenderer::ClearForFrame();		// before ConstructRenderList
	// CClock::CalcEnvMapTimeMultiplicator
if(gbRenderWater)
	CRenderer::RenderWater();	// actually CMattRenderer::RenderWater
	// CClock::ms_EnvMapTimeMultiplicator = 1.0f;
	// cWorldStream::ClearDynamics
	/// CRenderer::ConstructRenderList();	// before PreRender
if(gbRenderWorld0)
	CRenderer::RenderWorld(0);	// roads
	// CMattRenderer::ResetRenderStates
	/// CRenderer::PreRender();	// has to be called before BeginUpdate because of cutscene shadows
	CCoronas::RenderReflections();
if(gbRenderWorld1)
	CRenderer::RenderWorld(1);	// opaque
if(gbRenderRoads)
	CRenderer::RenderRoads();

	CRenderer::RenderPeds();

if(gbRenderBoats)
	CRenderer::RenderBoats();
//if(gbRenderFadingInUnderwaterEntities)
//	CRenderer::RenderFadingInUnderwaterEntities();

if(gbRenderEverythingBarRoads)
	CRenderer::RenderEverythingBarRoads();
	// seam fixer
	// moved this:
	// CRenderer::RenderFadingInEntities();
}

void
RenderScene_new(void)
{
	CClouds::Render();
	DoRWRenderHorizon();

	MattRenderScene();
	DefinedState();
	// CMattRenderer::ResetRenderStates
	// moved CRenderer::RenderBoats to before transparent water
}

// TODO
bool FredIsInFirstPersonCam(void) { return false; }
void
RenderEffects_new(void)
{
	CShadows::RenderStaticShadows();
	// CRenderer::GenerateEnvironmentMap
	CShadows::RenderStoredShadows();
	CSkidmarks::Render();
	CRubbish::Render();

	// these aren't really effects
	DefinedState();
	if(FredIsInFirstPersonCam()){
		DefinedState();
		C3dMarkers::Render();	// normally rendered in CSpecialFX::Render()
if(gbRenderWorld2)
		CRenderer::RenderWorld(2);	// transparent
if(gbRenderVehicles)
		CRenderer::RenderVehicles();
	}else{
		// flipped these two, seems to give the best result
if(gbRenderWorld2)
		CRenderer::RenderWorld(2);	// transparent
if(gbRenderVehicles)
		CRenderer::RenderVehicles();
	}
	// better render these after transparent world
if(gbRenderFadingInEntities)
	CRenderer::RenderFadingInEntities();

	// actual effects here
	CGlass::Render();
	// CMattRenderer::ResetRenderStates
	DefinedState();
	CWeather::RenderRainStreaks();
	// CWeather::AddSnow
	CWaterCannons::Render();
	CAntennas::Render();
	CSpecialFX::Render();
	CCoronas::Render();
	CParticle::Render();
	CPacManPickups::Render();
	CWeaponEffects::Render();
	CPointLights::RenderFogEffect();
	CMovingThings::Render();
	CRenderer::RenderFirstPersonVehicle();
}
#endif

void
RenderScene(void)
{
#ifdef NEW_RENDERER
	if(gbNewRenderer){
		RenderScene_new();
		return;
	}
#endif
	CClouds::Render();
	DoRWRenderHorizon();
	CRenderer::RenderRoads();
	CCoronas::RenderReflections();
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	CRenderer::RenderEverythingBarRoads();
	CRenderer::RenderBoats();
	DefinedState();
	CWaterLevel::RenderWater();
	CRenderer::RenderFadingInEntities();
#ifndef SQUEEZE_PERFORMANCE
	CRenderer::RenderVehiclesButNotBoats();
#endif
	CWeather::RenderRainStreaks();
}

void
RenderDebugShit(void)
{
	CTheScripts::RenderTheScriptDebugLines();
#ifndef FINAL
	if(gbShowCollisionLines)
		CRenderer::RenderCollisionLines();
	ThePaths.DisplayPathData();
	CDebug::DrawLines();
	DefinedState();
#endif
}

void
RenderEffects(void)
{
#ifdef NEW_RENDERER
	if(gbNewRenderer){
		RenderEffects_new();
		return;
	}
#endif
	CGlass::Render();
	CWaterCannons::Render();
	CSpecialFX::Render();
	CShadows::RenderStaticShadows();
	CShadows::RenderStoredShadows();
	CSkidmarks::Render();
	CAntennas::Render();
	CRubbish::Render();
	CCoronas::Render();
	CParticle::Render();
	CPacManPickups::Render();
	CWeaponEffects::Render();
	CPointLights::RenderFogEffect();
	CMovingThings::Render();
	CRenderer::RenderFirstPersonVehicle();
}

void
Render2dStuff(void)
{
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);

	CReplay::Display();
	CPickups::RenderPickUpText();

	if(TheCamera.m_WideScreenOn
#ifdef CUTSCENE_BORDERS_SWITCH
		&& CMenuManager::m_PrefsCutsceneBorders
#endif
		)
		TheCamera.DrawBordersForWideScreen();

	CPed *player = FindPlayerPed();
	int weaponType = 0;
	if(player)
		weaponType = player->GetWeapon()->m_eWeaponType;

	bool firstPersonWeapon = false;
	int cammode = TheCamera.Cams[TheCamera.ActiveCam].Mode;
	if(cammode == CCam::MODE_SNIPER ||
	   cammode == CCam::MODE_SNIPER_RUNABOUT ||
	   cammode == CCam::MODE_ROCKETLAUNCHER ||
	   cammode == CCam::MODE_ROCKETLAUNCHER_RUNABOUT)
		firstPersonWeapon = true;

	// Draw black border for sniper and rocket launcher
	if((weaponType == WEAPONTYPE_SNIPERRIFLE || weaponType == WEAPONTYPE_ROCKETLAUNCHER) && firstPersonWeapon){
		CRGBA black(0, 0, 0, 255);

		// top and bottom strips
		if (weaponType == WEAPONTYPE_ROCKETLAUNCHER) {
			CSprite2d::DrawRect(CRect(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT / 2 - SCREEN_SCALE_Y(180)), black);
			CSprite2d::DrawRect(CRect(0.0f, SCREEN_HEIGHT / 2 + SCREEN_SCALE_Y(170), SCREEN_WIDTH, SCREEN_HEIGHT), black);
		}
		else {
			CSprite2d::DrawRect(CRect(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT / 2 - SCREEN_SCALE_Y(210)), black);
			CSprite2d::DrawRect(CRect(0.0f, SCREEN_HEIGHT / 2 + SCREEN_SCALE_Y(210), SCREEN_WIDTH, SCREEN_HEIGHT), black);
		}
		CSprite2d::DrawRect(CRect(0.0f, 0.0f, SCREEN_WIDTH / 2 - SCREEN_SCALE_X(210), SCREEN_HEIGHT), black);
		CSprite2d::DrawRect(CRect(SCREEN_WIDTH / 2 + SCREEN_SCALE_X(210), 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT), black);
	}

	MusicManager.DisplayRadioStationName();
	TheConsole.Display();
#ifdef GTA_SCENE_EDIT
	if(CSceneEdit::m_bEditOn)
		CSceneEdit::Draw();
	else
#endif
		CHud::Draw();
	CUserDisplay::OnscnTimer.ProcessForDisplay();
	CMessages::Display();
	CDarkel::DrawMessages();
	CGarages::PrintMessages();
	CPad::PrintErrorMessage();
	CFont::DrawFonts();

#ifdef DEBUGMENU
	DebugMenuRender();
#endif
}

void
RenderMenus(void)
{
	if (FrontEndMenuManager.m_bMenuActive)
	{
		PUSH_MEMID(MEMID_FRONTEND);
		FrontEndMenuManager.DrawFrontEnd();
		POP_MEMID();
	}
}

void
Render2dStuffAfterFade(void)
{
#ifndef MASTER
	DisplayGameDebugText();
#endif

	CHud::DrawAfterFade();
	CFont::DrawFonts();
}

void
Idle(void *arg)
{
#ifdef ASPECT_RATIO_SCALE
	CDraw::SetAspectRatio(CDraw::FindAspectRatio());
#endif

	CTimer::Update();

	tbInit();

	CSprite2d::InitPerFrame();
	CFont::InitPerFrame();

	// We're basically merging FrontendIdle and Idle (just like TheGame on PS2)
#ifdef PS2_SAVE_DIALOG
	// Only exists on PC FrontendIdle, probably some PS2 bug fix
	if (FrontEndMenuManager.m_bMenuActive)
		CSprite2d::SetRecipNearClip();
	
	if (FrontEndMenuManager.m_bGameNotLoaded) {
		CPad::UpdatePads();
		FrontEndMenuManager.Process();
	} else {
		PUSH_MEMID(MEMID_GAME_PROCESS);
		CPointLights::InitPerFrame();
		tbStartTimer(0, "CGame::Process");
		CGame::Process();
		tbEndTimer("CGame::Process");
		POP_MEMID();

		tbStartTimer(0, "DMAudio.Service");
		DMAudio.Service();
		tbEndTimer("DMAudio.Service");
	}

	if (RsGlobal.quit)
		return;
#else

	PUSH_MEMID(MEMID_GAME_PROCESS);
	CPointLights::InitPerFrame();

	tbStartTimer(0, "CGame::Process");
	CGame::Process();
	tbEndTimer("CGame::Process");
	POP_MEMID();

	tbStartTimer(0, "DMAudio.Service");
	DMAudio.Service();
	tbEndTimer("DMAudio.Service");
#endif

	if(CGame::bDemoMode && CTimer::GetTimeInMilliseconds() > (3*60 + 30)*1000 && !CCutsceneMgr::IsCutsceneProcessing()){
		WANT_TO_LOAD = false;
		FrontEndMenuManager.m_bWantToRestart = true;
		return;
	}

	if(FrontEndMenuManager.m_bWantToRestart || FOUND_GAME_TO_LOAD)
	{
		return;
	}
	
	SetLightsWithTimeOfDayColour(Scene.world);

	if(arg == nil)
		return;

	PUSH_MEMID(MEMID_RENDER);

	if((!FrontEndMenuManager.m_bMenuActive || FrontEndMenuManager.m_bRenderGameInMenu) &&
	   TheCamera.GetScreenFadeStatus() != FADE_2)
	{
#if defined(GTA_PC) && !defined(RW_GL3) && defined(FIX_BUGS)
		// This is from SA, but it's nice for windowed mode
		if (!FrontEndMenuManager.m_bRenderGameInMenu) {
			RwV2d pos;
			pos.x = SCREEN_WIDTH / 2.0f;
			pos.y = SCREEN_HEIGHT / 2.0f;
			RsMouseSetPos(&pos);
		}
#endif

		PUSH_MEMID(MEMID_RENDERLIST);
		tbStartTimer(0, "CnstrRenderList");
#ifdef NEW_RENDERER
		if(gbNewRenderer){
			CWorld::AdvanceCurrentScanCode();	// don't think this is even necessary
			CRenderer::ClearForFrame();
		}
#endif
		CRenderer::ConstructRenderList();
		tbEndTimer("CnstrRenderList");

		tbStartTimer(0, "PreRender");
		CRenderer::PreRender();
		tbEndTimer("PreRender");
		POP_MEMID();

#ifdef FIX_BUGS
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void *)FALSE); // TODO: temp? this fixes OpenGL render but there should be a better place for this
		// This has to be done BEFORE RwCameraBeginUpdate
		RwCameraSetFarClipPlane(Scene.camera, CTimeCycle::GetFarClip());
		RwCameraSetFogDistance(Scene.camera, CTimeCycle::GetFogStart());
#endif

		if(CWeather::LightningFlash && !CCullZones::CamNoRain()){
			if(!DoRWStuffStartOfFrame_Horizon(255, 255, 255, 255, 255, 255, 255))
				goto popret;
		}else{
			if(!DoRWStuffStartOfFrame_Horizon(CTimeCycle::GetSkyTopRed(), CTimeCycle::GetSkyTopGreen(), CTimeCycle::GetSkyTopBlue(),
						CTimeCycle::GetSkyBottomRed(), CTimeCycle::GetSkyBottomGreen(), CTimeCycle::GetSkyBottomBlue(),
						255))
				goto popret;
		}

		DefinedState();

#ifndef FIX_BUGS
		RwCameraSetFarClipPlane(Scene.camera, CTimeCycle::GetFarClip());
		RwCameraSetFogDistance(Scene.camera, CTimeCycle::GetFogStart());
#endif

		tbStartTimer(0, "RenderScene");
		RenderScene();
		tbEndTimer("RenderScene");

#ifdef EXTENDED_PIPELINES
		CustomPipes::EnvMapRender();
#endif

		RenderDebugShit();
		RenderEffects();

		if((TheCamera.m_BlurType == MOTION_BLUR_NONE || TheCamera.m_BlurType == MOTION_BLUR_LIGHT_SCENE) &&
		   TheCamera.m_ScreenReductionPercentage > 0.0f)
		        TheCamera.SetMotionBlurAlpha(150);

#ifdef SCREEN_DROPLETS
		CPostFX::GetBackBuffer(Scene.camera);
		ScreenDroplets::Process();
		ScreenDroplets::Render();
#endif

		tbStartTimer(0, "RenderMotionBlur");
		TheCamera.RenderMotionBlur();
		tbEndTimer("RenderMotionBlur");

		tbStartTimer(0, "Render2dStuff");
		Render2dStuff();
		tbEndTimer("Render2dStuff");
	}else{
#ifdef ASPECT_RATIO_SCALE
		CameraSize(Scene.camera, nil, SCREEN_VIEWWINDOW, SCREEN_ASPECT_RATIO);
#else
		CameraSize(Scene.camera, nil, SCREEN_VIEWWINDOW, DEFAULT_ASPECT_RATIO);
#endif
		CVisibilityPlugins::SetRenderWareCamera(Scene.camera);
		RwCameraClear(Scene.camera, &gColourTop, CLEARMODE);
		if(!RsCameraBeginUpdate(Scene.camera))
			goto popret;
	}

#ifdef PS2_SAVE_DIALOG
	if (FrontEndMenuManager.m_bMenuActive)
		DefinedState();
#endif
	tbStartTimer(0, "RenderMenus");
	RenderMenus();
	tbEndTimer("RenderMenus");

#ifdef PS2_MENU
	if ( TheMemoryCard.m_bWantToLoad )
		goto popret;
#endif

	tbStartTimer(0, "DoFade");
	DoFade();
	tbEndTimer("DoFade");

	tbStartTimer(0, "Render2dStuff-Fade");
	Render2dStuffAfterFade();
	tbEndTimer("Render2dStuff-Fade");

	CCredits::Render();


	if (gbShowTimebars)
		tbDisplay();

	DoRWStuffEndOfFrame();

	POP_MEMID();	// MEMID_RENDER

	if(g_SlowMode) 
		ProcessSlowMode();
	return;

popret:	POP_MEMID();	// MEMID_RENDER
}

void
FrontendIdle(void)
{
#ifdef ASPECT_RATIO_SCALE
	CDraw::SetAspectRatio(CDraw::FindAspectRatio());
#endif

	CTimer::Update();
	CSprite2d::SetRecipNearClip(); // this should be on InitialiseRenderWare according to PS2 asm. seems like a bug fix
	CSprite2d::InitPerFrame();
	CFont::InitPerFrame();
	CPad::UpdatePads();
	FrontEndMenuManager.Process();

	if(RsGlobal.quit)
		return;

#ifdef ASPECT_RATIO_SCALE
	CameraSize(Scene.camera, nil, SCREEN_VIEWWINDOW, SCREEN_ASPECT_RATIO);
#else
	CameraSize(Scene.camera, nil, SCREEN_VIEWWINDOW, DEFAULT_ASPECT_RATIO);
#endif
	CVisibilityPlugins::SetRenderWareCamera(Scene.camera);
	RwCameraClear(Scene.camera, &gColourTop, CLEARMODE);
	if(!RsCameraBeginUpdate(Scene.camera))
		return;

	DefinedState(); // seems redundant, but breaks resolution change.
	RenderMenus();
	DoFade();
	Render2dStuffAfterFade();
//	CFont::DrawFonts(); // redundant
	DoRWStuffEndOfFrame();
}

void
InitialiseGame(void)
{
#if defined(_3DS) && defined(ENABLE_3DS_BOTTOM_LOADING)
	BottomLoadingActive = true;
	ResetLoadingScreenBar();
#endif
	LoadingScreen(nil, nil, "loadsc0");
	CGame::Initialise("DATA\\GTA3.DAT");
#if defined(_3DS) && defined(ENABLE_3DS_BOTTOM_LOADING)
	ClearBottomLoadingScreen();
#endif
}

RsEventStatus
AppEventHandler(RsEvent event, void *param)
{
	switch( event )
	{
		case rsINITIALIZE:
		{
			CGame::InitialiseOnceBeforeRW();
			return RsInitialize() ? rsEVENTPROCESSED : rsEVENTERROR;
		}

		case rsCAMERASIZE:
		{
											
			CameraSize(Scene.camera, (RwRect *)param,
				SCREEN_VIEWWINDOW, DEFAULT_ASPECT_RATIO);
			
			return rsEVENTPROCESSED;
		}

		case rsRWINITIALIZE:
		{
			return Initialise3D(param) ? rsEVENTPROCESSED : rsEVENTERROR;
		}

		case rsRWTERMINATE:
		{
			Terminate3D();

			return rsEVENTPROCESSED;
		}

		case rsTERMINATE:
		{
			CGame::FinalShutdown();

			return rsEVENTPROCESSED;
		}

		case rsPLUGINATTACH:
		{
			return PluginAttach() ? rsEVENTPROCESSED : rsEVENTERROR;
		}

		case rsINPUTDEVICEATTACH:
		{
			AttachInputDevices();

			return rsEVENTPROCESSED;
		}

		case rsIDLE:
		{
			Idle(param);

			return rsEVENTPROCESSED;
		}

		case rsFRONTENDIDLE:
		{
#ifdef PS2_SAVE_DIALOG
			Idle((void*)1);
#else
			FrontendIdle();
#endif

			return rsEVENTPROCESSED;
		}

		case rsACTIVATE:
		{
			param ? DMAudio.ReacquireDigitalHandle() : DMAudio.ReleaseDigitalHandle();

			return rsEVENTPROCESSED;
		}

		default:
		{
			return rsEVENTNOTPROCESSED;
		}
	}
}

#ifndef MASTER
void
TheModelViewer(void)
{
#if (defined(GTA_PS2) || defined(GTA_XBOX))
	//TODO
#else
	// This is III Mobile code. III Xbox code run it like main function, which is impossible to implement on PC's state machine implementation.
	// Also we want 2D things initialized in here to print animation ids etc., our additions for that marked with X

#ifdef ASPECT_RATIO_SCALE
	CDraw::SetAspectRatio(CDraw::FindAspectRatio()); // X
#endif
	CAnimViewer::Update();
	CTimer::Update();
	SetLightsWithTimeOfDayColour(Scene.world);
	CRenderer::ConstructRenderList();
	DoRWStuffStartOfFrame(CTimeCycle::GetSkyTopRed(), CTimeCycle::GetSkyTopGreen(), CTimeCycle::GetSkyTopBlue(),
		CTimeCycle::GetSkyBottomRed(), CTimeCycle::GetSkyBottomGreen(), CTimeCycle::GetSkyBottomBlue(),
		255);

	CSprite2d::InitPerFrame(); // X
	CFont::InitPerFrame(); // X
	DefinedState();
	CVisibilityPlugins::InitAlphaEntityList();
	CAnimViewer::Render();
	Render2dStuff(); // X
	DoRWStuffEndOfFrame();
#endif
}
#endif


#ifdef GTA_PS2
void TheGame(void)
{
	printf("Into TheGame!!!\n");

	PUSH_MEMID(MEMID_GAME);	// NB: not popped

	CTimer::Initialise();

#if GTA_VERSION <= GTA3_PS2_160
	CGame::Initialise();
#else
	CGame::Initialise("DATA\\GTA3.DAT");
#endif

	Const char *splash = GetRandomSplashScreen(); // inlined here

	LoadingScreen("Starting Game", NULL, splash);

#ifdef GTA_PS2
	if (   TheMemoryCard.CheckCardInserted(CARD_ONE) == CMemoryCard::NO_ERR_SUCCESS
		&& TheMemoryCard.ChangeDirectory(CARD_ONE, TheMemoryCard.Cards[CARD_ONE].dir)
		&& TheMemoryCard.FindMostRecentFileName(CARD_ONE, TheMemoryCard.MostRecentFile) == true
		&& TheMemoryCard.CheckDataNotCorrupt(TheMemoryCard.MostRecentFile))
	{
		strcpy(TheMemoryCard.LoadFileName, TheMemoryCard.MostRecentFile);
		TheMemoryCard.b_FoundRecentSavedGameWantToLoad = true;

		if (CMenuManager::m_PrefsLanguage != TheMemoryCard.GetLanguageToLoad())
		{
			CMenuManager::m_PrefsLanguage = TheMemoryCard.GetLanguageToLoad();
			TheText.Unload();
			TheText.Load();
		}

		CGame::currLevel = TheMemoryCard.GetLevelToLoad();
	}
#else
	//TODO
#endif

	while (true)
	{
		if (WANT_TO_LOAD)
		{
			Const char *splash1 = GetLevelSplashScreen(CGame::currLevel);
			LoadSplash(splash1);
		}

		WANT_TO_LOAD = false;

		CTimer::Update();

		while (!(FrontEndMenuManager.m_bWantToRestart || FOUND_GAME_TO_LOAD))
		{
			CSprite2d::InitPerFrame();
			CFont::InitPerFrame();

			PUSH_MEMID(MEMID_GAME_PROCESS)
			CPointLights::InitPerFrame();
			CGame::Process();
			POP_MEMID();

			DMAudio.Service();

			if (CGame::bDemoMode && CTimer::GetTimeInMilliseconds() > (3*60 + 30)*1000 && !CCutsceneMgr::IsCutsceneProcessing())
			{
				WANT_TO_LOAD = false;
				FrontEndMenuManager.m_bWantToRestart = true;
				break;
			}

			if (FrontEndMenuManager.m_bWantToRestart || FOUND_GAME_TO_LOAD)
				break;

			SetLightsWithTimeOfDayColour(Scene.world);

			PUSH_MEMID(MEMID_RENDER);

			if ((!FrontEndMenuManager.m_bMenuActive || FrontEndMenuManager.m_bRenderGameInMenu == true) && TheCamera.GetScreenFadeStatus() != FADE_2 )
			{

				PUSH_MEMID(MEMID_RENDERLIST);
				CRenderer::ConstructRenderList();
				CRenderer::PreRender();
				POP_MEMID();

#ifdef FIX_BUGS
				// This has to be done BEFORE RwCameraBeginUpdate
				RwCameraSetFarClipPlane(Scene.camera, CTimeCycle::GetFarClip());
				RwCameraSetFogDistance(Scene.camera, CTimeCycle::GetFogStart());
#endif

				if (CWeather::LightningFlash && !CCullZones::CamNoRain())
					DoRWStuffStartOfFrame_Horizon(255, 255, 255, 255, 255, 255, 255);
				else
					DoRWStuffStartOfFrame_Horizon(CTimeCycle::GetSkyTopRed(), CTimeCycle::GetSkyTopGreen(), CTimeCycle::GetSkyTopBlue(), CTimeCycle::GetSkyBottomRed(), CTimeCycle::GetSkyBottomGreen(), CTimeCycle::GetSkyBottomBlue(), 255);

				DefinedState();
#ifndef FIX_BUGS
				RwCameraSetFarClipPlane(Scene.camera, CTimeCycle::GetFarClip());
				RwCameraSetFogDistance(Scene.camera, CTimeCycle::GetFogStart());
#endif

				RenderScene();
				RenderDebugShit();
				RenderEffects();

				if ((TheCamera.m_BlurType == MOTION_BLUR_NONE || TheCamera.m_BlurType == MOTION_BLUR_LIGHT_SCENE) && TheCamera.m_ScreenReductionPercentage > 0.0f)
					TheCamera.SetMotionBlurAlpha(150);
				TheCamera.RenderMotionBlur();

				Render2dStuff();
			}
			else
			{
#ifdef ASPECT_RATIO_SCALE
				CameraSize(Scene.camera, nil, SCREEN_VIEWWINDOW, SCREEN_ASPECT_RATIO);
#else
				CameraSize(Scene.camera, nil, SCREEN_VIEWWINDOW, DEFAULT_ASPECT_RATIO);
#endif
				CVisibilityPlugins::SetRenderWareCamera(Scene.camera);
				RwCameraClear(Scene.camera, &gColourTop, CLEARMODE);
				RsCameraBeginUpdate(Scene.camera);
			}

			RenderMenus();

			if (WANT_TO_LOAD)
			{
				POP_MEMID();	// MEMID_RENDER
				break;
			}

			DoFade();
			Render2dStuffAfterFade();
			CCredits::Render();

			DoRWStuffEndOfFrame();

			while (frameCount < 2)
				;

			frameCount = 0;

			CTimer::Update();

			POP_MEMID():	// MEMID_RENDER

			if (g_SlowMode)
				ProcessSlowMode();
		}

		CPad::ResetCheats();
		CPad::StopPadsShaking();
		DMAudio.ChangeMusicMode(MUSICMODE_DISABLE);
		CGame::ShutDownForRestart();
		CTimer::Stop();

		if (FrontEndMenuManager.m_bWantToRestart || FOUND_GAME_TO_LOAD)
		{
			if (FOUND_GAME_TO_LOAD)
			{
				FrontEndMenuManager.m_bWantToRestart = true;
				WANT_TO_LOAD = true;
			}

			CGame::InitialiseWhenRestarting();
			DMAudio.ChangeMusicMode(MUSICMODE_GAME);
			FrontEndMenuManager.m_bWantToRestart = false;

			continue;
		}

		break;
	}

	DMAudio.Terminate();
}


void SystemInit()
{
#ifdef __MWERKS__
	mwInit();
#endif
	
#ifdef USE_CUSTOM_ALLOCATOR
	InitMemoryMgr();
#endif
	
#ifdef GTA_PS2
	CFileMgr::InitCdSystem();
	
	char path[256];
	
	sprintf(path, "cdrom0:\\%s%s;1", "SYSTEM\\", "IOPRP23.IMG");
	
	sceSifInitRpc(0);
	
	while ( !sceSifRebootIop(path) )
		;
	while( !sceSifSyncIop() )
		;
	
	sceSifInitRpc(0);
	
	CFileMgr::InitCdSystem();
	
	sceFsReset();
#endif

	CFileMgr::Initialise();
	
#ifdef GTA_PS2
	CFileMgr::InitCd();
	
	char modulepath[256];
	
	strcpy(modulepath, "cdrom0:\\");
	strcat(modulepath, "SYSTEM\\");
	strcat(modulepath, "SIO2MAN.IRX");
	LoadModule(modulepath);
	
	strcpy(modulepath, "cdrom0:\\");
	strcat(modulepath, "SYSTEM\\");
	strcat(modulepath, "PADMAN.IRX");
	LoadModule(modulepath);
	
	strcpy(modulepath, "cdrom0:\\");
	strcat(modulepath, "SYSTEM\\");
	strcat(modulepath, "LIBSD.IRX");
	LoadModule(modulepath);
	
	strcpy(modulepath, "cdrom0:\\");
	strcat(modulepath, "SYSTEM\\");
	strcat(modulepath, "SDRDRV.IRX");
	LoadModule(modulepath);
	
	strcpy(modulepath, "cdrom0:\\");
	strcat(modulepath, "SYSTEM\\");
	strcat(modulepath, "MCMAN.IRX");
	LoadModule(modulepath);
	
	strcpy(modulepath, "cdrom0:\\");
	strcat(modulepath, "SYSTEM\\");
	strcat(modulepath, "MCSERV.IRX");
	LoadModule(modulepath);
#endif
	

#ifdef GTA_PS2
	ThreadParam param;
	
	param.entry = &IdleThread;
	param.stack = idleThreadStack;
	param.stackSize = 2048;
	param.initPriority = 127;
	param.gpReg = &_gp;
	
	int thread = CreateThread(&param);
	StartThread(thread, NULL);
#else
	//
#endif
	
#ifdef GTA_PS2_STUFF
	CPad::Initialise();
#endif
	CPad::GetPad(0)->Mode = 0;
	
	CGame::frenchGame = false;
	CGame::germanGame = false;
	CGame::nastyGame = true;
	CMenuManager::m_PrefsAllowNastyGame = true;
	
#ifdef GTA_PS2
	int32 lang = sceScfGetLanguage();
	if ( lang  == SCE_ITALIAN_LANGUAGE )
		CMenuManager::m_PrefsLanguage = LANGUAGE_ITALIAN;
	else if ( lang  == SCE_SPANISH_LANGUAGE )
		CMenuManager::m_PrefsLanguage = LANGUAGE_SPANISH;
	else if ( lang  == SCE_GERMAN_LANGUAGE )
	{
		CGame::germanGame = true;
		CGame::nastyGame = false;
		CMenuManager::m_PrefsAllowNastyGame = false;
		CMenuManager::m_PrefsLanguage = LANGUAGE_GERMAN;
	}
	else if ( lang  == SCE_FRENCH_LANGUAGE )
	{
		CGame::frenchGame = true;
		CGame::nastyGame = false;
		CMenuManager::m_PrefsAllowNastyGame = false;
		CMenuManager::m_PrefsLanguage = LANGUAGE_FRENCH;
	}
	else
		CMenuManager::m_PrefsLanguage = LANGUAGE_AMERICAN;
	
	FrontEndMenuManager.InitialiseMenuContentsAfterLoadingGame();
#else
	//
#endif
	
#ifdef GTA_PS2
	TheMemoryCard.Init();
#endif
}

int VBlankCounter(int ca)
{
	frameCount++;
	ExitHandler();
	return 0;
}

// linked against by RW!
extern "C" void WaitVBlank(void)
{
	int32 startFrame = frameCount;
	while(startFrame == frameCount);
}

void GameInit()
{
	if ( !gameAlreadyInitialised )
	{
#ifdef GTA_PS2
		char path[256];
		
		strcpy(path, "cdrom0:\\");
		strcat(path, "SYSTEM\\");
		strcat(path, "CDSTREAM.IRX");
		LoadModule(path);
		
		strcpy(path, "cdrom0:\\");
		strcat(path, "SYSTEM\\");
		strcat(path, "SAMPMAN.IRX");
		LoadModule(path);
		
		strcpy(path, "cdrom0:\\");
		strcat(path, "SYSTEM\\");
		strcat(path, "MUSICSTR.IRX");
		LoadModule(path);
#endif
		CdStreamInit(MAX_CDCHANNELS);
		
#ifdef GTA_PS2
		Initialise3D(); //no params
#else
		//TODO
#endif
		
#ifdef GTA_PS2
		char *files[] =
		{
			"\\ANIM\\CUTS.IMG;1",
			"\\ANIM\\CUTS.DIR;1",
			"\\ANIM\\PED.IFP;1",
			"\\MODELS\\FRONTEND.TXD;1",
			"\\MODELS\\FONTS.TXD;1",
			"\\MODELS\\HUD.TXD;1",
			"\\MODELS\\PARTICLE.TXD;1",
			"\\MODELS\\MISC.TXD;1",
			"\\MODELS\\GENERIC.TXD;1",
			"\\MODELS\\GTA3.DIR;1",
			// TODO: japanese?
#ifdef GTA_PAL
			"\\TEXT\\ENGLISH.GXT;1",
			"\\TEXT\\FRENCH.GXT;1",
			"\\TEXT\\GERMAN.GXT;1",
			"\\TEXT\\ITALIAN.GXT;1",
			"\\TEXT\\SPANISH.GXT;1",
#else
			"\\TEXT\\AMERICAN.GXT;1",
#endif
			"\\TXD\\LOADSC0.TXD;1",
			"\\TXD\\LOADSC1.TXD;1",
			"\\TXD\\LOADSC2.TXD;1",
			"\\TXD\\LOADSC3.TXD;1",
			"\\TXD\\LOADSC4.TXD;1",
			"\\TXD\\LOADSC5.TXD;1",
			"\\TXD\\LOADSC6.TXD;1",
			"\\TXD\\LOADSC7.TXD;1",
			"\\TXD\\LOADSC8.TXD;1",
			"\\TXD\\LOADSC9.TXD;1",
			"\\TXD\\LOADSC10.TXD;1",
			"\\TXD\\LOADSC11.TXD;1",
			"\\TXD\\LOADSC12.TXD;1",
			"\\TXD\\LOADSC13.TXD;1",
			"\\TXD\\LOADSC14.TXD;1",
			"\\TXD\\LOADSC15.TXD;1",
			"\\TXD\\LOADSC16.TXD;1",
			"\\TXD\\LOADSC17.TXD;1",
			"\\TXD\\LOADSC18.TXD;1",
			"\\TXD\\LOADSC19.TXD;1",
			"\\TXD\\LOADSC20.TXD;1",
			"\\TXD\\LOADSC21.TXD;1",
			"\\TXD\\LOADSC22.TXD;1",
			"\\TXD\\LOADSC23.TXD;1",
			"\\TXD\\LOADSC24.TXD;1",
			"\\TXD\\LOADSC25.TXD;1",
			"\\TXD\\NEWS.TXD;1",
			"\\MODELS\\COLL\\GENERIC.COL;1",
			"\\MODELS\\COLL\\INDUST.COL;1",
			"\\MODELS\\COLL\\COMMER.COL;1",
			"\\MODELS\\COLL\\SUBURB.COL;1",
			"\\MODELS\\COLL\\WEAPONS.COL;1",
			"\\MODELS\\COLL\\VEHICLES.COL;1",
			"\\MODELS\\COLL\\PEDS.COL;1",
			"\\MODELS\\GENERIC\\AIR_VLO.DFF;1",
			"\\MODELS\\GENERIC\\WEAPONS.DFF;1",
			"\\MODELS\\GENERIC\\WHEELS.DFF;1",
			"\\MODELS\\GENERIC\\LOPLYGUY.DFF;1",
			"\\MODELS\\GENERIC\\ARROW.DFF;1",
			"\\MODELS\\GENERIC\\ZONECYLB.DFF;1",
			"\\DATA\\MAPS\\COMNTOP.IPL;1",
			"\\DATA\\MAPS\\COMNBTM.IPL;1",
			"\\DATA\\MAPS\\COMSE.IPL;1",
			"\\DATA\\MAPS\\COMSW.IPL;1",
			"\\DATA\\MAPS\\CULL.IPL;1",
			"\\DATA\\MAPS\\INDUSTNE.IPL;1",
			"\\DATA\\MAPS\\INDUSTNW.IPL;1",
			"\\DATA\\MAPS\\INDUSTSE.IPL;1",
			"\\DATA\\MAPS\\INDUSTSW.IPL;1",
			"\\DATA\\MAPS\\SUBURBNE.IPL;1",
			"\\DATA\\MAPS\\SUBURBSW.IPL;1",
			"\\DATA\\MAPS\\OVERVIEW.IPL;1",
			"\\DATA\\MAPS\\PROPS.IPL;1",
			"\\DATA\\MAPS\\GTA3.IDE;1",
			"\\DATA\\PATHS\\FLIGHT.DAT;1",
			"\\DATA\\PATHS\\FLIGHT2.DAT;1",
			"\\DATA\\PATHS\\FLIGHT3.DAT;1",
			"\\DATA\\PATHS\\FLIGHT4.DAT;1",
			"\\DATA\\PATHS\\TRACKS.DAT;1",
			"\\DATA\\PATHS\\TRACKS2.DAT;1",
			"\\DATA\\PATHS\\CHASE0.DAT;1",
			"\\DATA\\PATHS\\CHASE1.DAT;1",
			"\\DATA\\PATHS\\CHASE2.DAT;1",
			"\\DATA\\PATHS\\CHASE3.DAT;1",
			"\\DATA\\PATHS\\CHASE4.DAT;1",
			"\\DATA\\PATHS\\CHASE5.DAT;1",
			"\\DATA\\PATHS\\CHASE6.DAT;1",
			"\\DATA\\PATHS\\CHASE7.DAT;1",
			"\\DATA\\PATHS\\CHASE10.DAT;1",
			"\\DATA\\PATHS\\CHASE11.DAT;1",
			"\\DATA\\PATHS\\CHASE14.DAT;1",
			"\\DATA\\PATHS\\CHASE16.DAT;1",
			"\\DATA\\PATHS\\CHASE18.DAT;1",
			"\\DATA\\PATHS\\CHASE19.DAT;1"
		};
		
		for ( int32 i = 0; i < ARRAY_SIZE(files); i++ )
			SkyRegisterFileOnCd([i]);
#endif
		
		CreateDebugFont();
		
#ifdef GTA_PS2
		AddIntcHandler(INTC_VBLANK_S, VBlankCounter, 0);
#endif
		
		CameraSize(Scene.camera, NULL, DEFAULT_VIEWWINDOW, DEFAULT_ASPECT_RATIO);
		
		CSprite2d::SetRecipNearClip();
		CTxdStore::Initialise();

		PUSH_MEMID(MEMID_TEXTURES);
		CFont::Initialise();
		CHud::Initialise();
		POP_MEMID();

		ValidateVersion();
		
#ifdef GTA_PS2
		sceCdCLOCK rtc;
		sceCdReadClock(&rtc);
		uint32 seed = rtc.minute + rtc.day;
		uint32 seed2 = (seed << 4)-seed;
		uint32 seed3 = (seed2 << 4)-seed2;
		srand ((seed3<<4)+rtc.second);
#else
		//TODO: mysrand();
#endif
		
		gameAlreadyInitialised = true;
	}
}

void PlayIntroMPEGs()
{
#ifdef GTA_PS2
	if (gameAlreadyInitialised)
		RpSkySuspend();

	InitMPEGPlayer();

#ifdef GTA_PAL
	PlayMPEG("cdrom0:\\MOVIES\\DMAPAL.PSS;1", false);

	if (CGame::frenchGame || CGame::germanGame)
		PlayMPEG("cdrom0:\\MOVIES\\INTROPAF.PSS;1", true);
	else
		PlayMPEG("cdrom0:\\MOVIES\\INTROPAL.PSS;1", true);
#else
	PlayMPEG("cdrom0:\\MOVIES\\DMANTSC.PSS;1", false);

	PlayMPEG("cdrom0:\\MOVIES\\INTRNTSC.PSS;1", true);
#endif

	ShutdownMPEGPlayer();

	if ( gameAlreadyInitialised )
		RpSkyResume();
#else
	//TODO
#endif
}

int
main(int argc, char *argv[])
{
#ifdef __MWERKS__
	mwInit(); // metrowerks initialisation
#endif

	SystemInit();
	
#ifdef GTA_PS2
	int32 r = TheMemoryCard.CheckCardStateAtGameStartUp(CARD_ONE);
		
	if ( r == CMemoryCard::ERR_DIRNOENTRY  || r == CMemoryCard::ERR_NOFORMAT )
	{
		GameInit();
		
		TheText.Unload();
		TheText.Load();
		
		CFont::Initialise();
		
		FrontEndMenuManager.DrawMemoryCardStartUpMenus();
	}else if(r == CMemoryCard::ERR_OPENNOENTRY || r == CMemoryCard::ERR_NONE){
		// eh?
	}
#endif

	PlayIntroMPEGs();

	GameInit();

	if ( CGame::frenchGame || CGame::germanGame )
		LoadingScreen(NULL, version_name, "loadsc24");
	else
		LoadingScreen(NULL, version_name, "loadsc0");
	
	DMAudio.Initialise();
	
	TheGame();
	
	CGame::ShutDown();
	
	RwEngineStop();
	RwEngineClose();
	RwEngineTerm();
	
#ifdef __MWERKS__
	mwExit(); // metrowerks shutdown
#endif
	
	return 0;
}
#endif
