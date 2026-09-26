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
#include "ModelInfo.h"
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
#include "MemoryHeap.h"
#include "SceneEdit.h"
#include "debugmenu.h"
#include "Clock.h"
#include "Occlusion.h"
#include "Ropes.h"
#include "postfx.h"
#include "custompipes.h"
#include "screendroplets.h"
#include "VarConsole.h"
#ifdef _3DS
#include <3ds/os.h>
#endif
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
#define TOTALNUMCHUNKS 95.0f
static float LoadingProgressRangeStart;
static float LoadingProgressRangeEnd;
static bool LoadingProgressRangeActive;
#ifdef _3DS
static double LastLoadingScreenDrawTime = -1000.0;
static const char *LastLoadingScreenTitle;
static const char *LastLoadingScreenDetail;
#endif

#if defined(_3DS) && defined(ENABLE_3DS_BOTTOM_LOADING)
namespace {
enum {
	BOTTOM_SCREEN_WIDTH = 320,
	BOTTOM_SCREEN_HEIGHT = 240,
	BOTTOM_SCREEN_BPP = 3
};
static bool BottomLoadingActive;

static inline void
BottomLoadingPutPixel(uint8 *frameBuffer, int x, int y, uint8 red, uint8 green, uint8 blue)
{
	if((uint32)x >= BOTTOM_SCREEN_WIDTH || (uint32)y >= BOTTOM_SCREEN_HEIGHT)
		return;
	// Native 3DS framebuffers are rotated: each logical X is one contiguous
	// 240-pixel column.  gfxInitDefault uses BGR8 for both screens.
	uint8 *pixel = frameBuffer + ((x * BOTTOM_SCREEN_HEIGHT) +
		(BOTTOM_SCREEN_HEIGHT - 1 - y)) * BOTTOM_SCREEN_BPP;
	pixel[0] = blue;
	pixel[1] = green;
	pixel[2] = red;
}

static void
BottomLoadingFillRect(uint8 *frameBuffer, int left, int top, int right, int bottom,
	uint8 red, uint8 green, uint8 blue)
{
	left = clamp(left, 0, BOTTOM_SCREEN_WIDTH);
	right = clamp(right, 0, BOTTOM_SCREEN_WIDTH);
	top = clamp(top, 0, BOTTOM_SCREEN_HEIGHT);
	bottom = clamp(bottom, 0, BOTTOM_SCREEN_HEIGHT);
	for(int x = left; x < right; x++)
		for(int y = top; y < bottom; y++)
			BottomLoadingPutPixel(frameBuffer, x, y, red, green, blue);
}

static void
BottomLoadingDrawChar(uint8 *frameBuffer, int x, int y, unsigned char character,
	uint8 red, uint8 green, uint8 blue)
{
	ConsoleFont *font = &consoleGetDefault()->font;
	if(character < font->asciiOffset || character >= font->asciiOffset + font->numChars)
		character = '?';
	const uint8 *glyph = font->gfx + (character - font->asciiOffset) * 8;
	for(int row = 0; row < 8; row++)
		for(int column = 0; column < 8; column++)
			if(glyph[row] & (0x80 >> column))
				BottomLoadingPutPixel(frameBuffer, x + column, y + row, red, green, blue);
}

static void
BottomLoadingDrawText(uint8 *frameBuffer, int y, const char *text,
	uint8 red, uint8 green, uint8 blue)
{
	if(text == nil)
		return;
	int length = strlen(text);
	if(length > 38)
		length = 38;
	int x = (BOTTOM_SCREEN_WIDTH - length * 8) / 2;
	for(int i = 0; i < length; i++)
		BottomLoadingDrawChar(frameBuffer, x + i * 8, y,
			(unsigned char)text[i], red, green, blue);
}

static void
DrawBottomLoadingScreen(const char *title, const char *detail, float progress,
	bool drawProgress)
{
	uint8 *frameBuffer = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, nil, nil);
	if(frameBuffer == nil)
		return;

	progress = clamp(progress, 0.0f, 1.0f);
	// Clearing the contiguous buffer is much cheaper than plotting 76,800
	// background pixels every refresh on the ARM11.
	memset(frameBuffer, 0,
		BOTTOM_SCREEN_WIDTH * BOTTOM_SCREEN_HEIGHT * BOTTOM_SCREEN_BPP);
	BottomLoadingFillRect(frameBuffer, 0, 0, 320, 3, 255, 110, 210);
	BottomLoadingFillRect(frameBuffer, 0, 237, 320, 240, 100, 40, 105);

	BottomLoadingDrawText(frameBuffer, drawProgress ? 72 : 108,
		drawProgress ? (detail ? detail : "Please wait") : title,
		220, 220, 230);

	if(drawProgress){
		char percent[16];
		sprintf(percent, "%3d%%", (int)(progress * 100.0f + 0.5f));
		BottomLoadingDrawText(frameBuffer, 108, percent, 255, 150, 225);

		const int barLeft = 12;
		const int barRight = 308;
		const int barTop = 145;
		const int barBottom = 161;
		BottomLoadingFillRect(frameBuffer, barLeft, barTop, barRight, barBottom, 40, 36, 52);
		BottomLoadingFillRect(frameBuffer, barLeft + 2, barTop + 2,
			barRight - 2, barBottom - 2, 92, 52, 92);
		int fillRight = barLeft + 2 + (int)((barRight - barLeft - 4) * progress + 0.5f);
		BottomLoadingFillRect(frameBuffer, barLeft + 2, barTop + 2,
			fillRight, barBottom - 2, 255, 110, 210);
	}

	GSPGPU_FlushDataCache(frameBuffer,
		BOTTOM_SCREEN_WIDTH * BOTTOM_SCREEN_HEIGHT * BOTTOM_SCREEN_BPP);
	// The normal RenderWare presentation path now swaps the two LCDs
	// independently.  This loading screen writes the native bottom framebuffer
	// directly, so it must present that buffer itself instead of relying on the
	// next top-screen RwCameraShowRaster call to swap both screens.
	gfxScreenSwapBuffers(GFX_BOTTOM, false);
}

static void
ClearBottomLoadingScreen(void)
{
	BottomLoadingActive = false;
	// Loading writes alternating backbuffers. Clear both so the panel cannot
	// reappear every other frame after normal gameplay starts.
	for(int i = 0; i < 2; i++){
		uint8 *frameBuffer = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, nil, nil);
		if(frameBuffer){
			memset(frameBuffer, 0,
				BOTTOM_SCREEN_WIDTH * BOTTOM_SCREEN_HEIGHT * BOTTOM_SCREEN_BPP);
			GSPGPU_FlushDataCache(frameBuffer,
				BOTTOM_SCREEN_WIDTH * BOTTOM_SCREEN_HEIGHT * BOTTOM_SCREEN_BPP);
		}
		gfxScreenSwapBuffers(GFX_BOTTOM, false);
	}
}
}
#endif

#if defined(_3DS) && defined(ENABLE_3DS_BOTTOM_RADAR)
namespace {
static RwCamera *BottomRadarCamera;
enum { NUM_BOTTOM_TOUCH_PARTS = 4 };
static RwTexture *BottomTouchOverlayTextures[NUM_BOTTOM_TOUCH_PARTS];

struct BottomTouchOverlayPart {
	int sourceX, sourceY;
	int width, height;
	int textureWidth, textureHeight;
};

static const BottomTouchOverlayPart BottomTouchOverlayParts[NUM_BOTTOM_TOUCH_PARTS] = {
	{ 23,  26, 125,  79, 128, 128 }, // L3
	{ 173, 26, 125,  79, 128, 128 }, // R3
	{ 28, 120, 256, 102, 256, 128 }, // Camera, main body
	{ 284,120,   8, 102,   8, 128 }  // Camera, feathered right edge
};

static RwCamera *
CreateBottomRadarCamera(void)
{
	RwCamera *camera = RwCameraCreate();
	if(camera == nil)
		return nil;

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
	// The menu map is a square 3x3 atlas.  Preserve its aspect ratio on the
	// 320x240 lower display and fill the side gutters with the map's ocean blue.
	CSprite2d::DrawRect(CRect(0.0f, 0.0f, 320.0f, 240.0f),
		CRGBA(103, 164, 218, 255));
	if(!FrontEndMenuManager.m_bSpritesLoaded)
		return;

	for(int row = 0; row < 3; row++) {
		for(int column = 0; column < 3; column++) {
			const int sprite = MENUSPRITE_MAPTOP01 + row * 3 + column;
			if(FrontEndMenuManager.m_aFrontEndSprites[sprite].m_pTexture == nil)
				continue;
			const float left = 40.0f + column * 80.0f;
			const float top = row * 80.0f;
			FrontEndMenuManager.m_aFrontEndSprites[sprite].Draw(
				CRect(left, top, left + 80.0f, top + 80.0f),
				CRGBA(255, 255, 255, 255));
		}
	}
}

static RwTexture *
CreateEmbeddedTexturePart(const uint8 *rgba, int sourceWidth,
	const BottomTouchOverlayPart &part)
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

	int32 rasterWidth, rasterHeight, depth, format;
	RwImageFindRasterFormat(image, rwRASTERTYPETEXTURE,
		&rasterWidth, &rasterHeight, &depth, &format);
	RwRaster *raster = RwRasterCreate(rasterWidth, rasterHeight, depth, format);
	if(raster == nil || RwRasterSetFromImage(raster, image) == nil) {
		if(raster) RwRasterDestroy(raster);
		RwImageDestroy(image);
		return nil;
	}
	RwImageDestroy(image);
	return RwTextureCreate(raster);
}

static bool
CreateBottomTouchOverlayTextures(void)
{
	unsigned char *rgba = nil;
	unsigned width = 0;
	unsigned height = 0;
	if(lodepng_decode32(&rgba, &width, &height,
		bottom_touch_bin, bottom_touch_bin_size) != 0 ||
		width != 320 || height != 240) {
		free(rgba);
		return false;
	}

	bool success = true;
	for(int i = 0; i < NUM_BOTTOM_TOUCH_PARTS; i++) {
		BottomTouchOverlayTextures[i] =
			CreateEmbeddedTexturePart(rgba, 320, BottomTouchOverlayParts[i]);
		if(BottomTouchOverlayTextures[i] == nil) {
			success = false;
			break;
		}
	}
	free(rgba);
	if(!success) {
		for(int i = 0; i < NUM_BOTTOM_TOUCH_PARTS; i++) {
			if(BottomTouchOverlayTextures[i]) {
				RwTextureDestroy(BottomTouchOverlayTextures[i]);
				BottomTouchOverlayTextures[i] = nil;
			}
		}
	}
	return success;
}

static void
BottomRadarDrawText(uint8 *frameBuffer, int x, int y, const char *text,
	uint8 red, uint8 green, uint8 blue)
{
#ifdef ENABLE_3DS_BOTTOM_LOADING
	if(text == nil)
		return;
	for(int i = 0; text[i] && x + i * 8 < 320; i++)
		BottomLoadingDrawChar(frameBuffer, x + i * 8, y,
			(unsigned char)text[i], red, green, blue);
#else
	(void)frameBuffer; (void)x; (void)y; (void)text;
	(void)red; (void)green; (void)blue;
#endif
}

static void
DrawBottomRadarStatus(CPlayerPed *player)
{
#ifdef ENABLE_3DS_BOTTOM_LOADING
	uint8 *frameBuffer = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, nil, nil);
	if(frameBuffer == nil || player == nil)
		return;
	GSPGPU_InvalidateDataCache(frameBuffer,
		BOTTOM_SCREEN_WIDTH * BOTTOM_SCREEN_HEIGHT * BOTTOM_SCREEN_BPP);

	CWeapon *weapon = player->GetWeapon();
	char value[24];
	BottomRadarDrawText(frameBuffer, 248, 65, "AMMO", 255, 185, 225);
	if(weapon) {
		sprintf(value, "%d/%d", weapon->m_nAmmoInClip, weapon->m_nAmmoTotal);
		BottomRadarDrawText(frameBuffer, 244, 78, value, 255, 255, 255);
	}

	sprintf(value, "%02d:%02d", CClock::GetHours(), CClock::GetMinutes());
	BottomRadarDrawText(frameBuffer, 260, 98, value, 255, 255, 255);
	sprintf(value, "$%07d", CWorld::Players[CWorld::PlayerInFocus].m_nVisibleMoney);
	BottomRadarDrawText(frameBuffer, 244, 111, value, 115, 235, 165);

	BottomRadarDrawText(frameBuffer, 248, 133, "HEALTH", 255, 185, 225);
	sprintf(value, "%03d", (int)(player->m_fHealth + 0.5f));
	BottomRadarDrawText(frameBuffer, 264, 146, value, 255, 255, 255);
	BottomLoadingFillRect(frameBuffer, 246, 159, 314, 167, 35, 40, 48);
	BottomLoadingFillRect(frameBuffer, 247, 160,
		247 + (int)(65.0f * clamp(player->m_fHealth, 0.0f, 100.0f) / 100.0f), 166,
		255, 92, 145);

	BottomRadarDrawText(frameBuffer, 248, 181, "ARMOUR", 190, 225, 255);
	sprintf(value, "%03d", (int)(player->m_fArmour + 0.5f));
	BottomRadarDrawText(frameBuffer, 264, 194, value, 255, 255, 255);
	BottomLoadingFillRect(frameBuffer, 246, 207, 314, 215, 35, 40, 48);
	BottomLoadingFillRect(frameBuffer, 247, 208,
		247 + (int)(65.0f * clamp(player->m_fArmour, 0.0f, 100.0f) / 100.0f), 214,
		125, 195, 255);

	GSPGPU_FlushDataCache(frameBuffer,
		BOTTOM_SCREEN_WIDTH * BOTTOM_SCREEN_HEIGHT * BOTTOM_SCREEN_BPP);
#else
	(void)player;
#endif
}

static void
DrawBottomOriginalHud(CPlayerPed *player)
{
	if(player == nil)
		return;

	const float hudYOffset = 10.0f;
	char ascii[32];
	wchar text[32];
	CWeapon *weapon = player->GetWeapon();
	CWeaponInfo *weaponInfo = CWeaponInfo::GetWeaponInfo(weapon->m_eWeaponType);

	CFont::SetBackgroundOff();
	CFont::SetJustifyOff();
	CFont::SetRightJustifyOff();
	CFont::SetCentreOn();
	CFont::SetCentreSize(78.0f);
	CFont::SetPropOn();
	CFont::SetDropShadowPosition(2);
	CFont::SetDropColor(CRGBA(0, 0, 0, 255));
	// Slot 0 is unarmed and slot 1 is melee. Their stale ammo fields can
	// contain zero or the following weapon's value, so never display them.
	if(weaponInfo->m_nWeaponSlot > 1 && weapon->m_eWeaponType != WEAPONTYPE_DETONATOR) {
		int ammoAmount = weaponInfo->m_nAmountofAmmunition;
		if(ammoAmount <= 1 || ammoAmount >= 1000)
			sprintf(ascii, "%d", weapon->m_nAmmoTotal);
		else if(weapon->m_eWeaponType == WEAPONTYPE_FLAMETHROWER)
			sprintf(ascii, "%d-%d", Min((weapon->m_nAmmoTotal - weapon->m_nAmmoInClip) / 10, 9999),
				weapon->m_nAmmoInClip / 10);
		else
			sprintf(ascii, "%d-%d", Min(weapon->m_nAmmoTotal - weapon->m_nAmmoInClip, 9999),
				weapon->m_nAmmoInClip);
		AsciiToUnicode(ascii, text);
		CFont::SetScale(0.52f, 0.78f);
		CFont::SetFontStyle(FONT_STANDARD);
		CFont::SetColor(CRGBA(255, 150, 225, 255));
		CFont::PrintString(280.0f, 64.0f + hudYOffset, text);
	}

	CFont::SetPropOff();
	CFont::SetFontStyle(FONT_HEADING);
	CFont::SetScale(0.52f, 0.78f);
	sprintf(ascii, "%02d:%02d", CClock::GetHours(), CClock::GetMinutes());
	AsciiToUnicode(ascii, text);
	CFont::SetColor(CRGBA(97, 194, 247, 255));
	CFont::PrintString(280.0f, 96.0f + hudYOffset, text);

	sprintf(ascii, "$%07d", CWorld::Players[CWorld::PlayerInFocus].m_nVisibleMoney);
	AsciiToUnicode(ascii, text);
	CFont::SetScale(0.44f, 0.67f);
	CFont::SetColor(CRGBA(0, 207, 133, 255));
	CFont::PrintString(280.0f, 128.0f + hudYOffset, text);

	sprintf(ascii, "%03d", (int)(player->m_fHealth + 0.5f));
	AsciiToUnicode(ascii, text);
	CFont::SetScale(0.52f, 0.78f);
	CFont::SetColor(CRGBA(255, 150, 225, 255));
	CFont::PrintString(288.0f, 160.0f + hudYOffset, text);
	AsciiToUnicode("{", text);
	CFont::SetScale(0.48f, 0.74f);
	CFont::PrintString(258.0f, 160.0f + hudYOffset, text);

	sprintf(ascii, "%03d", (int)(player->m_fArmour + 0.5f));
	AsciiToUnicode(ascii, text);
	CFont::SetScale(0.52f, 0.78f);
	CFont::SetColor(CRGBA(185, 185, 185, 255));
	CFont::SetDropShadowPosition(2);
	CFont::PrintString(288.0f, 192.0f + hudYOffset, text);
	AsciiToUnicode("<", text);
	CFont::SetScale(0.48f, 0.74f);
	CFont::PrintString(258.0f, 192.0f + hudYOffset, text);
	CFont::DrawFonts();
}

static void
RenderBottomRadar(void)
{
#ifdef ENABLE_3DS_BOTTOM_LOADING
	if(BottomLoadingActive)
		return;
#endif
	// gameAlreadyInitialised is never set on this port's normal new-game path
	// (the original assignment is commented out). The front-end flag is the
	// authoritative lifecycle guard on 3DS.
	const bool coldStartMenu = FrontEndMenuManager.m_bGameNotLoaded &&
		FrontEndMenuManager.m_bMenuActive;
	if(!coldStartMenu && (FrontEndMenuManager.m_bGameNotLoaded ||
		FrontEndMenuManager.m_bMenuActive))
		return;

	if(BottomRadarCamera == nil)
		BottomRadarCamera = CreateBottomRadarCamera();
	if(BottomRadarCamera == nil)
		return;

	CRGBA clear(0, 0, 0, 255);
	RwCameraClear(BottomRadarCamera, &clear.rwRGBA,
		rwCAMERACLEARIMAGE | rwCAMERACLEARZ | rwCAMERACLEARSTENCIL);
	if(coldStartMenu) {
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
	// A simple early return leaves the previous gameplay radar frozen on the
	// lower LCD.  During cutscenes (and other scripted widescreen shots), submit
	// the cleared raster so location and player status cannot show through.
	if(TheCamera.m_WideScreenOn || CCutsceneMgr::IsCutsceneProcessing() ||
		!CHud::m_Wants_To_Draw_Hud) {
		RwCameraShowRaster(BottomRadarCamera, nil, rwRASTERFLIPDONTWAIT);
		return;
	}

	CPlayerPed *player = FindPlayerPed();
	if(player == nil) {
		RwCameraShowRaster(BottomRadarCamera, nil, rwRASTERFLIPDONTWAIT);
		return;
	}
	if(!RwCameraBeginUpdate(BottomRadarCamera))
		return;

	CSprite2d::InitPerFrame();
	CRadar::m_bDrawingBottomScreen = true;
	if(FrontEndMenuManager.m_PrefsRadarMode != 2) {
		CRadar::DrawMap();
		CRadar::DrawBlips();
	}
	CRadar::m_bDrawingBottomScreen = false;

	// Right-hand 80-pixel status rail. The active weapon icon uses the same
	// HUD sprite table as the stock upper-screen HUD.
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	CSprite2d::DrawRect(CRect(240.0f, 0.0f, 320.0f, 240.0f), CRGBA(8, 10, 16, 145));
	CSprite2d::DrawRect(CRect(240.0f, 0.0f, 242.0f, 240.0f), CRGBA(255, 105, 190, 210));
	CWeapon *weapon = player->GetWeapon();
	const float hudYOffset = 10.0f;
	int weaponType = weapon->m_eWeaponType;
	CWeaponInfo *weaponInfo = CWeaponInfo::GetWeaponInfo((eWeaponType)weaponType);
	if(weaponInfo->m_nModelId <= 0) {
		if(weaponType >= 0 && weaponType < NUM_HUD_SPRITES)
			CHud::Sprites[weaponType].Draw(CRect(250.0f, 3.0f + hudYOffset, 310.0f, 61.0f + hudYOffset),
				CRGBA(255, 255, 255, 255));
	} else {
		CBaseModelInfo *weaponModel = CModelInfo::GetModelInfo(weaponInfo->m_nModelId);
		if(weaponModel) {
			RwTexDictionary *weaponTxd = CTxdStore::GetSlot(weaponModel->GetTxdSlot())->texDict;
			RwTexture *weaponIcon = weaponTxd ?
				RwTexDictionaryFindNamedTexture(weaponTxd, weaponModel->GetModelName()) : nil;
			if(weaponIcon) {
				static CSprite2d icon;
				icon.m_pTexture = weaponIcon;
				icon.Draw(CRect(250.0f, 3.0f + hudYOffset, 310.0f, 61.0f + hudYOffset), CRGBA(255, 255, 255, 255));
				icon.m_pTexture = nil;
			}
		}
	}
	DrawBottomOriginalHud(player);

	if(CPad::Is3DSTouchOverlayVisible()) {
		if(BottomTouchOverlayTextures[0]) {
			for(int i = 0; i < NUM_BOTTOM_TOUCH_PARTS; i++) {
				const BottomTouchOverlayPart &part = BottomTouchOverlayParts[i];
				CSprite2d overlay;
				overlay.m_pTexture = BottomTouchOverlayTextures[i];
				float maxU = (float)part.width / part.textureWidth;
				float maxV = (float)part.height / part.textureHeight;
				overlay.Draw(CRect((float)part.sourceX, (float)part.sourceY,
					(float)(part.sourceX + part.width),
					(float)(part.sourceY + part.height)),
					CRGBA(255, 255, 255, 255),
					0.0f, 0.0f, maxU, 0.0f,
					0.0f, maxV, maxU, maxV);
				overlay.m_pTexture = nil;
			}
		}
	}

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
		if(BottomTouchOverlayTextures[i]) {
			RwTextureDestroy(BottomTouchOverlayTextures[i]);
			BottomTouchOverlayTextures[i] = nil;
		}
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

#ifdef GTA_PS2
#define WANT_TO_LOAD TheMemoryCard.m_bWantToLoad
#define FOUND_GAME_TO_LOAD TheMemoryCard.b_FoundRecentSavedGameWantToLoad
#else
#define WANT_TO_LOAD FrontEndMenuManager.m_bWantToLoad
#define FOUND_GAME_TO_LOAD b_FoundRecentSavedGameWantToLoad
#endif

#ifdef NEW_RENDERER
bool gbNewRenderer;
#endif
#ifdef FIX_BUGS
// need to clear stencil for mblur fx. no idea why it works in the original game
// also for clearing out water rects in new renderer
#define CLEARMODE (rwCAMERACLEARZ | rwCAMERACLEARSTENCIL)
#else
#define CLEARMODE (rwCAMERACLEARZ)
#endif

bool bDisplayNumOfAtomicsRendered = false;
bool bDisplayPosn = false;

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

	CDraw::CalculateAspectRatio();
	CameraSize(Scene.camera, nil, SCREEN_VIEWWINDOW, SCREEN_ASPECT_RATIO);
	CVisibilityPlugins::SetRenderWareCamera(Scene.camera);
	RwCameraClear(Scene.camera, &TopColor.rwRGBA, CLEARMODE);

	if(!RsCameraBeginUpdate(Scene.camera))
		return false;

	CSprite2d::InitPerFrame();

	if(Alpha != 0)
		CSprite2d::DrawRect(CRect(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT), BottomColor, BottomColor, TopColor, TopColor);

	return true;
}

bool
DoRWStuffStartOfFrame_Horizon(int16 TopRed, int16 TopGreen, int16 TopBlue, int16 BottomRed, int16 BottomGreen, int16 BottomBlue, int16 Alpha)
{
	CDraw::CalculateAspectRatio();
	CameraSize(Scene.camera, nil, SCREEN_VIEWWINDOW, SCREEN_ASPECT_RATIO);
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

	if(CDraw::FadeValue != 0 || FrontEndMenuManager.m_PrefsBrightness < 256){
		CSprite2d *splash = LoadSplash(nil);

		CRGBA fadeColor;
		CRect rect;
		int fadeValue = CDraw::FadeValue;
		float brightness = Min(FrontEndMenuManager.m_PrefsBrightness, 256);
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

		TheCamera.GetScreenRect(rect);
		CSprite2d::DrawRect(rect, fadeColor);

		if(CDraw::FadeValue != 0 && TheCamera.m_FadeTargetIsSplashScreen){
			RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
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
#ifndef LIBRW
	if (!RtAnimInitialize())
	{
		return FALSE;
	}
#endif
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
#define NUM_PREALLOC_ATOMICS 1800
#define NUM_PREALLOC_CLUMPS 80
#define NUM_PREALLOC_FRAMES 2600
#define NUM_PREALLOC_GEOMETRIES 850
#define NUM_PREALLOC_TEXDICTS 121
#define NUM_PREALLOC_TEXTURES 1700
#define NUM_PREALLOC_MATERIALS 2600
bool preAlloc;

void
PreAllocateRwObjects(void)
{
	int i;

	PUSH_MEMID(MEMID_PRE_ALLOC);
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
	POP_MEMID();
}
#endif

static RwBool 
Initialise3D(void *param)
{
	PUSH_MEMID(MEMID_RENDER);

#ifndef MASTER
	VarConsole.Add("Display number of atomics rendered", &bDisplayNumOfAtomicsRendered, true);
	VarConsole.Add("Display posn and framerate", &bDisplayPosn, true);
#endif

		if (RsRwInitialize(param))
		{
			POP_MEMID();

	#ifdef ENABLE_3DS_BOTTOM_RADAR
			// Allocate the small overlay textures before world textures fill linear
			// memory. Creating them on the first touch could make the 3DS texture
			// allocator shrink unrelated game textures under memory pressure.
			CreateBottomTouchOverlayTextures();
	#endif

	#ifdef DEBUGMENU
		DebugMenuInit();
		DebugMenuPopulate();
#endif // !DEBUGMENU
		return CGame::InitialiseRenderWare();
	}
	POP_MEMID();

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
	static int splashIndex[12] = {
		1, 2,
		3, 4,
		5, 11,
		6, 8,
		9, 10,
		7, 12
	};

	index = splashIndex[2*index2 + CGeneral::GetRandomNumberInRange(0, 2)];
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
	LoadingProgressRangeStart = 0.0f;
	LoadingProgressRangeEnd = 0.0f;
	LoadingProgressRangeActive = false;
#ifdef _3DS
	LastLoadingScreenDrawTime = -1000.0;
	LastLoadingScreenTitle = nil;
	LastLoadingScreenDetail = nil;
#endif
}

void
SetLoadingScreenProgress(float progress)
{
	progress = clamp(progress, 0.0f, 1.0f);
	/* Streamed IFPs reuse the animation-loader progress callbacks.  A script
	 * started near the end of boot can therefore report the animation stage's
	 * old 61-65% range after the global bar has already reached 97%.  Loading
	 * progress is cumulative within one ResetLoadingScreenBar() cycle, so never
	 * allow a subsystem-local estimate to move the global bar backwards. */
	float current = clamp(NumberOfChunksLoaded / TOTALNUMCHUNKS, 0.0f, 1.0f);
	if(progress < current)
		progress = current;
	NumberOfChunksLoaded = progress * TOTALNUMCHUNKS;
#ifdef _3DS
	/* The normal 200 ms loading-screen throttle can otherwise hide the final
	 * update when the last two boot stages complete in the same frame. */
	if(progress >= 1.0f)
		LastLoadingScreenDrawTime = -1000.0;
#endif
	LoadingScreen(nil, nil, nil);
}

void
SetLoadingScreenStage(float progress, const char *detail)
{
	progress = clamp(progress, 0.0f, 1.0f);
	float current = clamp(NumberOfChunksLoaded / TOTALNUMCHUNKS, 0.0f, 1.0f);
	if(progress < current)
		progress = current;
	NumberOfChunksLoaded = progress * TOTALNUMCHUNKS;
	LoadingScreen("Loading the Game", detail, nil);
}

void
BeginLoadingScreenProgressRange(float start, float end)
{
	LoadingProgressRangeStart = clamp(start, 0.0f, 1.0f);
	LoadingProgressRangeEnd = clamp(end, LoadingProgressRangeStart, 1.0f);
	LoadingProgressRangeActive = true;
	SetLoadingScreenProgress(LoadingProgressRangeStart);
}

void
UpdateLoadingScreenProgressRange(float progress)
{
	if(!LoadingProgressRangeActive)
		return;
	progress = clamp(progress, 0.0f, 1.0f);
	SetLoadingScreenProgress(LoadingProgressRangeStart +
		(LoadingProgressRangeEnd - LoadingProgressRangeStart) * progress);
}

void
EndLoadingScreenProgressRange(void)
{
	if(LoadingProgressRangeActive)
		SetLoadingScreenProgress(LoadingProgressRangeEnd);
	LoadingProgressRangeActive = false;
}

void
LoadingScreen(const char *str1, const char *str2, const char *splashscreen)
{
	CSprite2d *splash;
	bool loadingOnly = str1 != nil && str2 == nil && !strcmp(str1, "Loading");
	bool drawProgress = str1 != nil && !loadingOnly;

#if defined(_3DS) && defined(ENABLE_3DS_BOTTOM_LOADING)
	// Once gameplay is live, streaming stalls are normally only one frame.
	// Keep the already-rendered world on screen instead of flashing any splash.
	// Real island transitions still use LoadingIslandScreen below.
	if(!BottomLoadingActive && !FrontEndMenuManager.m_bGameNotLoaded &&
		!FrontEndMenuManager.m_bMenuActive)
		return;
#endif

#if defined(_3DS) && defined(ENABLE_3DS_LOADING_PROGRESS)
	drawProgress = !loadingOnly;
	bool stageChanged = false;
	if(loadingOnly){
		stageChanged = true;
		LastLoadingScreenTitle = str1;
		LastLoadingScreenDetail = nil;
	}else if(str1){
		stageChanged = str1 != LastLoadingScreenTitle ||
			(LastLoadingScreenTitle && strcmp(str1, LastLoadingScreenTitle) != 0);
		LastLoadingScreenTitle = str1;
	}
	if(str2){
		stageChanged = stageChanged || str2 != LastLoadingScreenDetail ||
			(LastLoadingScreenDetail && strcmp(str2, LastLoadingScreenDetail) != 0);
		LastLoadingScreenDetail = str2;
	}
	str1 = LastLoadingScreenTitle;
	str2 = LastLoadingScreenDetail;
	// Keep progress accounting cheap.  The old loading screen rendered every
	// file and substantially extended load time on 3DS.
	double now = RsTimer();
	if(!stageChanged && now - LastLoadingScreenDrawTime < 200.0)
		return;
	LastLoadingScreenDrawTime = now;
	// Start on loadsc0, then honour the stock milestone splash requests from
	// Game.cpp.  Calls with no splash keep the current texture resident, so only
	// the original major stages (such as generic textures and animations) swap
	// TXDs instead of every detailed progress update.
	if(::splash.m_pTexture == nil)
		splashscreen = "LOADSC0";
	else if(splashscreen == nil)
		splashscreen = nil;
#elif defined(DISABLE_LOADING_SCREEN)
	if (str1 && str2)
		return;
#endif

#if !defined(RANDOMSPLASH) && !(defined(_3DS) && defined(ENABLE_3DS_LOADING_PROGRESS))
	splashscreen = "LOADSC0";
#endif

	splash = LoadSplash(splashscreen);

#if defined(_3DS) && defined(ENABLE_3DS_BOTTOM_LOADING)
	if(BottomLoadingActive)
		DrawBottomLoadingScreen(str1, str2,
			clamp(NumberOfChunksLoaded/TOTALNUMCHUNKS, 0.0f, 1.0f), !loadingOnly);
	drawProgress = false;
#endif

#ifndef GTA_PS2
	if(RsGlobal.quit)
		return;
#endif

	if(DoRWStuffStartOfFrame(0, 0, 0, 0, 0, 0, 255)){
		CSprite2d::SetRecipNearClip();
		CSprite2d::InitPerFrame();
		CFont::InitPerFrame();
		DefinedState();
		RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSCLAMP);
		splash->Draw(CRect(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT), CRGBA(255, 255, 255, 255));

		if(drawProgress){
#if !defined(_3DS) || !defined(ENABLE_3DS_LOADING_PROGRESS)
			NumberOfChunksLoaded += 1;
#endif

#ifndef RANDOMSPLASH
			float hpos = SCREEN_SCALE_X(40);
			float length = SCREEN_WIDTH - SCREEN_SCALE_X(80);
			float top = SCREEN_HEIGHT - SCREEN_SCALE_Y(14);
			float bottom = top + SCREEN_SCALE_Y(5);
#else
			float hpos = SCREEN_STRETCH_X(40);
			float length = SCREEN_STRETCH_X(440);
			// this is rather weird
			float top = SCREEN_STRETCH_Y(407.4f - 7.0f/3.0f);
			float bottom = SCREEN_STRETCH_Y(407.4f + 7.0f/3.0f);
#endif

			CSprite2d::DrawRect(CRect(hpos-1.0f, top-1.0f, hpos+length+1.0f, bottom+1.0f), CRGBA(40, 53, 68, 255));

			CSprite2d::DrawRect(CRect(hpos, top, hpos+length, bottom), CRGBA(155, 50, 125, 255));

			length *= clamp(NumberOfChunksLoaded/TOTALNUMCHUNKS, 0.0f, 1.0f);
			CSprite2d::DrawRect(CRect(hpos, top, hpos+length, bottom), CRGBA(255, 150, 225, 255));

			// this is done by the game but is unused
			CFont::SetBackgroundOff();
			CFont::SetScale(SCREEN_SCALE_X(2), SCREEN_SCALE_Y(2));
			CFont::SetPropOn();
			CFont::SetRightJustifyOn();
			CFont::SetDropShadowPosition(1);
			CFont::SetDropColor(CRGBA(0, 0, 0, 255));
			CFont::SetFontStyle(FONT_HEADING);

#ifdef CHATTYSPLASH
			// my attempt
			if(str1){
				static wchar tmpstr[80];
				float yscale = SCREEN_SCALE_Y(0.9f);
				top -= 45*yscale;
				CFont::SetScale(SCREEN_SCALE_X(0.75f), yscale);
				CFont::SetPropOn();
				CFont::SetRightJustifyOff();
				CFont::SetFontStyle(FONT_STANDARD);
				CFont::SetColor(CRGBA(255, 255, 255, 255));
				AsciiToUnicode(str1, tmpstr);
				CFont::PrintString(hpos, top, tmpstr);
				top += 22*yscale;
				if (str2) {
					AsciiToUnicode(str2, tmpstr);
					CFont::PrintString(hpos, top, tmpstr);
				}
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

	splash = LoadSplash(nil);
	if(!DoRWStuffStartOfFrame(0, 0, 0, 0, 0, 0, 255))
		return;

	CSprite2d::SetRecipNearClip();
	CSprite2d::InitPerFrame();
	CFont::InitPerFrame();
	DefinedState();
	CRGBA col = CRGBA(255, 255, 255, 255);
	CRGBA col2 = CRGBA(0, 0, 0, 255);
	CSprite2d::DrawRect(CRect(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT), col2);
	splash->Draw(CRect(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT), col, col, col, col);
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
		if ( CPad::GetPad(1)->GetSelectJustDown() || CPad::GetPad(1)->GetStart() )
			break;
		
		if ( stop )
		{
			CTimer::Stop();
			stop = false;
		}
		
		CPad::UpdatePads();
		
		RwCameraBeginUpdate(Scene.camera);
		RwCameraEndUpdate(Scene.camera);
		
	} while (!CPad::GetPad(1)->GetSelectJustDown() && !CPad::GetPad(1)->GetStart());
	
	
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
	char name[11];
	char area[5];
	CRect rect;
};

tZonePrint ZonePrint[] =
{
	{ "DOWNTOWN", "GM", CRect(-1500.0f, 1500.0f, -300.0f, 980.0f)},
	{ "DOWNTOWS", "KB", CRect(-1200.0f, 980.0f, -300.0f, 435.0f)},
	{ "GOLF", "NT", CRect(-300.0f, 660.0f, 320.0f, -255.0f)},
	{ "LITTLEHA", "AG", CRect(-1250.0f, -310.0f, -746.0f, -926.0f)},
	{ "HAITI", "CJ", CRect(-1355.0f, 30.0f, -637.0f, -304.0f)},
	{ "HAITIN", "SM", CRect(-1355.0f, 435.0f, -637.0f, 30.0f)},
	{ "DOCKS", "AW", CRect(-1122.0f, -926.0f, -609.0f, -1575.0f)},
	{ "AIRPORT", "NT", CRect(-2000.0f, 200.0f, -871.0f, -2000.0f)},
	{ "STARISL", "CJ", CRect(-724.0f, -320.0f, -40.0f, -380.0f)},
	{ "CENT.ISLA", "NT", CRect(-163.0f, 1260.0f,  120.0f, 830.0f)},
	{ "MALL", "AW", CRect( 300.0f, 1266.0f,  483.0f, 995.0f)},
	{ "MANSION", "KB", CRect(-724.0f, -500.0f, -40.0f, -670.0f)},
	{ "NBEACH", "AS", CRect( 120.0f,  1340.0f,  900.0f,  600.0f)},
	{ "NBEACHBT", "AS", CRect( 200.0f, 680.0f,  660.0f, -50.0f)},
	{ "NBEACHW", "AS", CRect(-93.0f, 80.0f,  410.0f, -680.0f)},
	{ "OCEANDRV", "AC", CRect( 200.0f, -964.0f,  955.0f, -1797.0f)},
	{ "OCEANDN", "WS", CRect( 400.0f, 50.0f,  955.0f,  -964.0f)},
	{ "WASHINGTN", "AC", CRect(-320.0f, -487.0f,  500.0f, -1200.0f)},
	{ "WASHINBTM", "AC", CRect(-255.0f, -1200.0f,  500.0f, -1690.0f)}
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

	sprintf(gString, "PreAlloc: %d blocks, %d bytes", gMainHeap.GetBlocksUsed(MEMID_PRE_ALLOC), gMainHeap.GetMemoryUsed(MEMID_PRE_ALLOC));
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

	sprintf(gString, "Streamed LODs: %d blocks, %d bytes", gMainHeap.GetBlocksUsed(MEMID_STREAM_LODS), gMainHeap.GetMemoryUsed(MEMID_STREAM_LODS));
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(24.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Streamed Textures: %d blocks, %d bytes", gMainHeap.GetBlocksUsed(MEMID_STREAM_TEXUTRES), gMainHeap.GetMemoryUsed(MEMID_STREAM_TEXUTRES));
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(24.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Streamed Collision: %d blocks, %d bytes", gMainHeap.GetBlocksUsed(MEMID_STREAM_COLLISION), gMainHeap.GetMemoryUsed(MEMID_STREAM_COLLISION));
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(24.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Streamed Animation: %d blocks, %d bytes", gMainHeap.GetBlocksUsed(MEMID_STREAM_ANIMATION), gMainHeap.GetMemoryUsed(MEMID_STREAM_ANIMATION));
	AsciiToUnicode(gString, gUString);
	CFont::PrintString(24.0f, y, gUString);
	y += 12.0f;

	sprintf(gString, "Ped Attr: %d blocks, %d bytes", gMainHeap.GetBlocksUsed(MEMID_PED_ATTR), gMainHeap.GetMemoryUsed(MEMID_PED_ATTR));
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
	static bool bDisplayCheatStr = false; // custom

#ifndef FINAL
	{
		SETTWEAKPATH("Debug");
		TWEAKBOOL(bDisplayPosn);
		TWEAKBOOL(bDisplayCheatStr);
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
	CFont::SetFontStyle(FONT_STANDARD);
	CFont::SetCentreOff();
	CFont::SetRightJustifyOff();
	CFont::SetWrapx(SCREEN_WIDTH);
	CFont::SetJustifyOff();
	CFont::SetBackGroundOnlyTextOff();
	CFont::SetColor(CRGBA(255, 108, 0, 255));
	CFont::PrintString(SCREEN_SCALE_X(10.0f), SCREEN_SCALE_Y(10.0f), ver);
	}
#endif // #ifdef DRAW_GAME_VERSION_TEXT

	FrameSamples++;
#ifdef FIX_HIGH_FPS_BUGS_ON_FRONTEND
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

	if ( bDisplayPosn )
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
		sprintf(str, "X:%4.0f Y:%4.0f Z:%4.0f F-%d %s-%s", pos.x, pos.y, pos.z, (int32)FramesPerSecond,
			ZonePrint[ZoneId].name, ZonePrint[ZoneId].area);

		AsciiToUnicode(str, ustr);
		
		CFont::SetPropOn();
		CFont::SetBackgroundOff();
		CFont::SetScale(SCREEN_SCALE_X(0.6f), SCREEN_SCALE_Y(0.8f));
		CFont::SetCentreOff();
		CFont::SetRightJustifyOff();
		CFont::SetJustifyOff();
		CFont::SetBackGroundOnlyTextOff();
		CFont::SetWrapx(SCREEN_STRETCH_X(DEFAULT_SCREEN_WIDTH));
		CFont::SetFontStyle(FONT_STANDARD);
		CFont::SetDropColor(CRGBA(0, 0, 0, 255));
		CFont::SetDropShadowPosition(2);
		CFont::SetColor(CRGBA(0, 0, 0, 255));
		CFont::PrintString(41.0f, 41.0f, ustr);
		
		CFont::SetColor(CRGBA(205, 205, 0, 255));
		CFont::PrintString(40.0f, 40.0f, ustr);
	}

	// custom
	if (bDisplayCheatStr)
	{
		sprintf(str, "%s", CPad::KeyBoardCheatString);
		AsciiToUnicode(str, ustr);

		CFont::SetPropOn();
		CFont::SetBackgroundOff();
		CFont::SetScale(SCREEN_SCALE_X(0.6f), SCREEN_SCALE_Y(0.8f));
		CFont::SetCentreOn();
		CFont::SetBackGroundOnlyTextOff();
		CFont::SetWrapx(SCREEN_STRETCH_X(DEFAULT_SCREEN_WIDTH));
		CFont::SetFontStyle(FONT_STANDARD);

		CFont::SetColor(CRGBA(0, 0, 0, 255));
		CFont::PrintString(SCREEN_SCALE_X(DEFAULT_SCREEN_WIDTH * 0.5f)+2.f, SCREEN_SCALE_FROM_BOTTOM(20.0f)+2.f, ustr);

		CFont::SetColor(CRGBA(255, 150, 225, 255));
		CFont::PrintString(SCREEN_SCALE_X(DEFAULT_SCREEN_WIDTH * 0.5f), SCREEN_SCALE_FROM_BOTTOM(20.0f), ustr);
	}
}
#endif

#ifdef NEW_RENDERER
bool gbRenderRoads = true;
bool gbRenderEverythingBarRoads = true;
bool gbRenderFadingInUnderwaterEntities = true;
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
	RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
	CWaterLevel::RenderWater();	// actually CMattRenderer::RenderWater
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

	// not sure where to put these since LCS has no underwater entities
if(gbRenderBoats)
	CRenderer::RenderBoats();
if(gbRenderFadingInUnderwaterEntities)
	CRenderer::RenderFadingInUnderwaterEntities();
	RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
if(gbRenderWater)
	CRenderer::RenderTransparentWater();

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
	CCoronas::RenderSunReflection();
	CWeather::RenderRainStreaks();
	// CWeather::AddSnow
	CWaterCannons::Render();
	CAntennas::Render();
	CSpecialFX::Render();
	CRopes::Render();
	CCoronas::Render();
	CParticle::Render();
	CPacManPickups::Render();
	CWeaponEffects::Render();
#ifdef _3DS
	if(!rw::c3d::stereoControlsActive())
		CPointLights::RenderFogEffect();
#else
	CPointLights::RenderFogEffect();
#endif
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
	CRenderer::RenderEverythingBarRoads();
	RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
	CWaterLevel::RenderWater();
	/* Keep the original pre-water boat pass.  On 3DS it supplies the submerged
	 * hull colour which the transparent water pass tints normally. */
	CRenderer::RenderBoats();
	CRenderer::RenderFadingInUnderwaterEntities();
	RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
	CWaterLevel::RenderTransparentWater();
#ifdef _3DS
	/* Ordinary cars stopped showing the near-water polygon corruption once they
	 * stayed in the normal post-water vehicle path.  Boats use the same vehicle
	 * MatFX pipeline, so draw their dedicated sorted list after all water passes
	 * as well.  Water depth rejects the submerged part of this corrective pass,
	 * leaving the correctly tinted first pass visible below the surface. */
	CRenderer::RenderBoats();
#endif
	CRenderer::RenderFadingInEntities();
	RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
	CWeather::RenderRainStreaks();
	CCoronas::RenderSunReflection();
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
	CRopes::Render();
	CShadows::RenderStaticShadows();
	CShadows::RenderStoredShadows();
#ifdef _3DS
	const bool performanceProfile = rw::c3d::performanceModeActive();
	CSkidmarks::Render();
	if(!performanceProfile && !rw::c3d::stereoControlsActive()){
		CAntennas::Render();
	}
	CRubbish::Render();
#else
	CSkidmarks::Render();
	CAntennas::Render();
	CRubbish::Render();
#endif
	CCoronas::Render();
	CParticle::Render();
	CPacManPickups::Render();
	CWeaponEffects::Render();
#ifdef _3DS
	if(!rw::c3d::stereoControlsActive())
		CPointLights::RenderFogEffect();
#else
	CPointLights::RenderFogEffect();
#endif
#ifdef _3DS
	if(!performanceProfile)
		CMovingThings::Render();
#else
	CMovingThings::Render();
#endif
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
	   cammode == CCam::MODE_ROCKETLAUNCHER_RUNABOUT ||
	   cammode == CCam::MODE_CAMERA)
		firstPersonWeapon = true;

	// Draw black border for sniper and rocket launcher
	if((weaponType == WEAPONTYPE_SNIPERRIFLE || weaponType == WEAPONTYPE_ROCKETLAUNCHER || weaponType == WEAPONTYPE_LASERSCOPE) && firstPersonWeapon){
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

	CSpecialFX::Render2DFXs();
	CUserDisplay::OnscnTimer.ProcessForDisplay();
	CMessages::Display();
	CDarkel::DrawMessages();
	CGarages::PrintMessages();
	CPad::PrintErrorMessage();
	CFont::DrawFonts();
#ifndef MASTER
	COcclusion::Render();
#endif

#ifdef DEBUGMENU
	DebugMenuRender();
#endif
}

void
RenderMenus(void)
{
	if (FrontEndMenuManager.m_bMenuActive)
	{
		FrontEndMenuManager.DrawFrontEnd();
	}
#ifndef MASTER
	else
		VarConsole.Check();
#endif
}

void
Render2dStuffAfterFade(void)
{
#ifndef MASTER
	DisplayGameDebugText();
#endif



#ifdef MOBILE_IMPROVEMENTS
	if (CDraw::FadeValue != 0)
#endif
	CHud::DrawAfterFade();
	CFont::DrawFonts();
	CCredits::Render();
}

static float
Apply3DSFarClipProfile(float farClip)
{
#ifdef _3DS
	return Min(farClip, Max(180.0f, farClip * rw::c3d::adaptiveWorldRangeScale()));
#else
	return farClip;
#endif
}

static float
Apply3DSFogProfile(float fogStart)
{
#ifdef _3DS
	return Min(fogStart, Max(120.0f, fogStart * rw::c3d::adaptiveWorldRangeScale()));
#else
	return fogStart;
#endif
}

void
Idle(void *arg)
{
#ifdef _3DS
	rw::c3d::ProfileLogicFrameScope profileLogicFrame;
#endif
	CTimer::Update();

	tbInit();

	CSprite2d::InitPerFrame();
	CFont::InitPerFrame();

	PUSH_MEMID(MEMID_GAME_PROCESS);
	CPointLights::InitPerFrame();

#ifdef _3DS
	uint64 profileGameplayStart = rw::c3d::profileTimerStart();
#endif
	tbStartTimer(0, "CGame::Process");
	CGame::Process();
	tbEndTimer("CGame::Process");
#ifdef _3DS
	rw::c3d::profileRecordGameplay(profileGameplayStart);
#endif
	POP_MEMID();

#ifdef _3DS
	uint64 profileAudioStart = rw::c3d::profileTimerStart();
#endif
	tbStartTimer(0, "DMAudio.Service");
	DMAudio.Service();
	tbEndTimer("DMAudio.Service");
#ifdef _3DS
	rw::c3d::profileRecordAudio(profileAudioStart);
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

#ifdef _3DS
	if(arg != nil && !FrontEndMenuManager.m_bMenuActive && rw::c3d::shouldSkipStereoFrame())
		return;
#endif
	
	SetLightsWithTimeOfDayColour(Scene.world);

	if(arg == nil)
		return;

	PUSH_MEMID(MEMID_RENDER);

	if(!FrontEndMenuManager.m_bMenuActive && TheCamera.GetScreenFadeStatus() != FADE_2)
	{
		// This is from SA, but it's nice for windowed mode
#if defined(GTA_PC) && !defined(RW_GL3)
		RwV2d pos;
		pos.x = SCREEN_WIDTH / 2.0f;
		pos.y = SCREEN_HEIGHT / 2.0f;
		RsMouseSetPos(&pos);
#endif

		tbStartTimer(0, "CnstrRenderList");
#ifdef PC_WATER
		CWaterLevel::PreCalcWaterGeometry();
#endif
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

#ifdef FIX_BUGS
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void *)FALSE); // TODO: temp? this fixes OpenGL render but there should be a better place for this
		// This has to be done BEFORE RwCameraBeginUpdate
		RwCameraSetFarClipPlane(Scene.camera, Apply3DSFarClipProfile(CTimeCycle::GetFarClip()));
		RwCameraSetFogDistance(Scene.camera, Apply3DSFogProfile(CTimeCycle::GetFogStart()));
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
		RwCameraSetFarClipPlane(Scene.camera, Apply3DSFarClipProfile(CTimeCycle::GetFarClip()));
		RwCameraSetFogDistance(Scene.camera, Apply3DSFogProfile(CTimeCycle::GetFogStart()));
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
		bool stereo3D = false;
#ifdef _3DS
		stereo3D = rw::c3d::stereoControlsActive();
#endif

#ifdef SCREEN_DROPLETS
		if(!stereo3D){
			CPostFX::GetBackBuffer(Scene.camera);
			ScreenDroplets::Process();
			ScreenDroplets::Render();
		}
#endif

		tbStartTimer(0, "RenderMotionBlur");
		if(!stereo3D)
			TheCamera.RenderMotionBlur();
		tbEndTimer("RenderMotionBlur");

		tbStartTimer(0, "Render2dStuff");
		Render2dStuff();
		tbEndTimer("Render2dStuff");
	}else{
		CDraw::CalculateAspectRatio();
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
	// CCredits::Render(); // They added it to function above and also forgot it here
#ifdef XBOX_MESSAGE_SCREEN
	FrontEndMenuManager.DrawOverlays();
#endif

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
	CDraw::CalculateAspectRatio();
	CTimer::Update();
	CSprite2d::SetRecipNearClip(); // this should be on InitialiseRenderWare according to PS2 asm. seems like a bug fix
	CSprite2d::InitPerFrame();
	CFont::InitPerFrame();
	CPad::UpdatePads();
	FrontEndMenuManager.Process();

	if(RsGlobal.quit)
		return;

	CameraSize(Scene.camera, nil, SCREEN_VIEWWINDOW, SCREEN_ASPECT_RATIO);
	CVisibilityPlugins::SetRenderWareCamera(Scene.camera);
	RwCameraClear(Scene.camera, &gColourTop, CLEARMODE);
	if(!RsCameraBeginUpdate(Scene.camera))
		return;

	DefinedState(); // seems redundant, but breaks resolution change.
	RenderMenus();
#ifdef XBOX_MESSAGE_SCREEN
	FrontEndMenuManager.DrawOverlays();
#endif
	DoFade();
	Render2dStuffAfterFade();
	CFont::DrawFonts();
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
	CGame::Initialise("DATA\\GTA_VC.DAT");
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
			FrontendIdle();

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

	// This is not original. Because;
	// 1- We want 2D things to be initalized, whereas original AnimViewer doesn't use them. my additions marked with X
	// 2- VC Mobile code run it like main function(as opposed to III and LCS), so it has it's own loop inside it, but our func. already called in a loop.

	CDraw::CalculateAspectRatio(); // X
	CAnimViewer::Update();
	SetLightsWithTimeOfDayColour(Scene.world);
	CRenderer::ConstructRenderList();
	DoRWStuffStartOfFrame(CTimeCycle::GetSkyTopRed()*0.5f, CTimeCycle::GetSkyTopGreen()*0.5f, CTimeCycle::GetSkyTopBlue()*0.5f,
		CTimeCycle::GetSkyBottomRed(), CTimeCycle::GetSkyBottomGreen(), CTimeCycle::GetSkyBottomBlue(),
		255);

	CSprite2d::SetRecipNearClip(); // X
	CSprite2d::InitPerFrame(); // X
	CFont::InitPerFrame(); // X
	DefinedState();
	CVisibilityPlugins::InitAlphaEntityList();
	CAnimViewer::Render();
	Render2dStuff(); // X
	DoRWStuffEndOfFrame();
	CTimer::Update();
#endif
}
#endif


#ifdef GTA_PS2
void TheGame(void)
{
	printf("Into TheGame!!!\n");

	PUSH_MEMID(MEMID_GAME);	// NB: not popped

	CTimer::Initialise();

	CGame::Initialise("DATA\\GTA3.DAT");

	Const char *splash = GetRandomSplashScreen(); // inlined here

	LoadingScreen("Starting Game", NULL, splash);

#ifdef GTA_PS2
	// TODO(MIAMI): not checked yet
	if (   TheMemoryCard.CheckCardInserted(CARD_ONE) == CMemoryCard::NO_ERR_SUCCESS
		&& TheMemoryCard.ChangeDirectory(CARD_ONE, TheMemoryCard.Cards[CARD_ONE].dir)
		&& TheMemoryCard.FindMostRecentFileName(CARD_ONE, TheMemoryCard.MostRecentFile) == true
		&& TheMemoryCard.CheckDataNotCorrupt(TheMemoryCard.MostRecentFile))
	{
		strcpy(TheMemoryCard.LoadFileName, TheMemoryCard.MostRecentFile);
		TheMemoryCard.b_FoundRecentSavedGameWantToLoad = true;

		if (FrontEndMenuManager.m_PrefsLanguage != TheMemoryCard.GetLanguageToLoad())
		{
			FrontEndMenuManager.m_PrefsLanguage = TheMemoryCard.GetLanguageToLoad();
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
		if (FOUND_GAME_TO_LOAD)
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
#ifdef _3DS
			uint64 profileGameplayStart = rw::c3d::profileTimerStart();
#endif
			CGame::Process();
#ifdef _3DS
			rw::c3d::profileRecordGameplay(profileGameplayStart);
#endif
			POP_MEMID();

#ifdef _3DS
			uint64 profileAudioStart = rw::c3d::profileTimerStart();
#endif
			DMAudio.Service();
#ifdef _3DS
			rw::c3d::profileRecordAudio(profileAudioStart);
#endif

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

			CRenderer::ConstructRenderList();

			if ((!FrontEndMenuManager.m_bMenuActive || FrontEndMenuManager.m_bRenderGameInMenu == true) && TheCamera.GetScreenFadeStatus() != FADE_2 )
			{
				CRenderer::PreRender();
				// TODO(MIAMI): something ps2all specific

#ifdef FIX_BUGS
				// This has to be done BEFORE RwCameraBeginUpdate
				RwCameraSetFarClipPlane(Scene.camera, Apply3DSFarClipProfile(CTimeCycle::GetFarClip()));
				RwCameraSetFogDistance(Scene.camera, Apply3DSFogProfile(CTimeCycle::GetFogStart()));
#endif

				if (CWeather::LightningFlash && !CCullZones::CamNoRain())
					DoRWStuffStartOfFrame_Horizon(255, 255, 255, 255, 255, 255, 255);
				else
					DoRWStuffStartOfFrame_Horizon(CTimeCycle::GetSkyTopRed(), CTimeCycle::GetSkyTopGreen(), CTimeCycle::GetSkyTopBlue(), CTimeCycle::GetSkyBottomRed(), CTimeCycle::GetSkyBottomGreen(), CTimeCycle::GetSkyBottomBlue(), 255);

				DefinedState();
#ifndef FIX_BUGS
				RwCameraSetFarClipPlane(Scene.camera, Apply3DSFarClipProfile(CTimeCycle::GetFarClip()));
				RwCameraSetFogDistance(Scene.camera, Apply3DSFogProfile(CTimeCycle::GetFogStart()));
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
				CameraSize(Scene.camera, NULL, SCREEN_VIEWWINDOW, SCREEN_ASPECT_RATIO);
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
#ifdef USE_CUSTOM_ALLOCATOR
	InitMemoryMgr();
#endif
	
#ifdef GTA_PS2
	CFileMgr::InitCdSystem();
	
	char path[256];
	
	sprintf(path, "cdrom0:\\%s%s;1", "SYSTEM\\", "IOPRP241.IMG");
	
	sceSifInitRpc(0);
	
	while ( !sceSifRebootIop(path) )
		;
	while( !sceSifSyncIop() )
		;
	
	sceSifInitRpc(0);
	
	CFileMgr::InitCdSystem();
	
	sceFsReset();

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

	strcpy(modulepath, "cdrom0:\\");
	strcat(modulepath, "SYSTEM\\");
	strcat(modulepath, "CDSTREAM.IRX");
	LoadModule(modulepath);

	strcpy(modulepath, "cdrom0:\\");
	strcat(modulepath, "SYSTEM\\");
	strcat(modulepath, "SAMPMAN2.IRX");
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
	FrontEndMenuManager.m_PrefsAllowNastyGame = true;
	
#ifdef GTA_PS2
	// TODO(MIAMI): this code probably went elsewhere?
	int32 lang = sceScfGetLanguage();
	if ( lang  == SCE_ITALIAN_LANGUAGE )
		FrontEndMenuManager.m_PrefsLanguage = LANGUAGE_ITALIAN;
	else if ( lang  == SCE_SPANISH_LANGUAGE )
		FrontEndMenuManager.m_PrefsLanguage = LANGUAGE_SPANISH;
	else if ( lang  == SCE_GERMAN_LANGUAGE )
	{
		CGame::germanGame = true;
		CGame::nastyGame = false;
		FrontEndMenuManager.m_PrefsAllowNastyGame = false;
		FrontEndMenuManager.m_PrefsLanguage = LANGUAGE_GERMAN;
	}
	else if ( lang  == SCE_FRENCH_LANGUAGE )
	{
		CGame::frenchGame = true;
		CGame::nastyGame = false;
		FrontEndMenuManager.m_PrefsAllowNastyGame = false;
		FrontEndMenuManager.m_PrefsLanguage = LANGUAGE_FRENCH;
	}
	else
		FrontEndMenuManager.m_PrefsLanguage = LANGUAGE_AMERICAN;
	
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

void GameInit(bool onlyRW)
{
	if(onlyRW)
	{
#ifdef GTA_PS2
		Initialise3D(nil);
#else
		Initialise3D(nil);	//TODO: window parameter
#endif
		gameAlreadyInitialised = true;
	}
	else
	{
		if ( !gameAlreadyInitialised )
#ifdef GTA_PS2
			Initialise3D(nil);
#else
			Initialise3D(nil);	//TODO: window parameter
#endif
		}

#ifdef GTA_PS2
		char *files[] =
		{
			"\\ANIM\\CUTS.IMG;1",
			"\\ANIM\\CUTS.DIR;1",
			"\\ANIM\\PED.IFP;1",
			"\\MODELS\\FRONTEN1.TXD;1",
			"\\MODELS\\FRONTEN2.TXD;1",
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
			"\\MODELS\\COLL\\GENERIC.COL;1",
			"\\MODELS\\COLL\\VEHICLES.COL;1",
			"\\MODELS\\COLL\\PEDS.COL;1",
			"\\MODELS\\COLL\\WEAPONS.COL;1",
			"\\MODELS\\GENERIC\\AIR_VLO.DFF;1",
			"\\MODELS\\GENERIC\\WHEELS.DFF;1",
			"\\MODELS\\GENERIC\\ARROW.DFF;1",
			"\\MODELS\\GENERIC\\ZONECYLB.DFF;1",
			"\\DATA\\HANDLING.CFG;1",
			"\\DATA\\SURFACE.DAT;1",
			"\\DATA\\PEDSTATS.DAT;1",
			"\\DATA\\TIMECYC.DAT;1",
			"\\DATA\\PARTICLE.CFG;1",
			"\\DATA\\DEFAULT.DAT;1",
			"\\DATA\\DEFAULT.IDE;1",
			"\\DATA\\GTA_VC.DAT;1",
			"\\DATA\\OBJECT.DAT;1",
			"\\DATA\\MAP.ZON;1",
			"\\DATA\\NAVIG.ZON;1",
			"\\DATA\\INFO.ZON;1",
			"\\DATA\\WATERPRO.DAT;1",
			"\\DATA\\MAIN.SCM;1",
			"\\DATA\\CARCOLS.DAT;1",
			"\\DATA\\PED.DAT;1",
			"\\DATA\\FISTFITE.DAT;1",
			"\\DATA\\WEAPON.DAT;1",
			"\\DATA\\PEDGRP.DAT;1",
			"\\DATA\\PATHS\\FLIGHT.DAT;1",
			"\\DATA\\PATHS\\FLIGHT2.DAT;1",
			"\\DATA\\PATHS\\FLIGHT3.DAT;1",
			"\\DATA\\PATHS\\SPATH0.DAT;1",
			"\\DATA\\MAPS\\LITTLEHA\\LITTLEHA.IDE;1",
			"\\DATA\\MAPS\\DOWNTOWN\\DOWNTOWN.IDE;1",
			"\\DATA\\MAPS\\DOWNTOWS\\DOWNTOWS.IDE;1",
			"\\DATA\\MAPS\\DOCKS\\DOCKS.IDE;1",
			"\\DATA\\MAPS\\WASHINTN\\WASHINTN.IDE;1",
			"\\DATA\\MAPS\\WASHINTS\\WASHINTS.IDE;1",
			"\\DATA\\MAPS\\OCEANDRV\\OCEANDRV.IDE;1",
			"\\DATA\\MAPS\\OCEANDN\\OCEANDN.IDE;1",
			"\\DATA\\MAPS\\GOLF\\GOLF.IDE;1",
			"\\DATA\\MAPS\\BRIDGE\\BRIDGE.IDE;1",
			"\\DATA\\MAPS\\STARISL\\STARISL.IDE;1",
			"\\DATA\\MAPS\\NBEACHBT\\NBEACHBT.IDE;1",
			"\\DATA\\MAPS\\NBEACHW\\NBEACHW.IDE;1",
			"\\DATA\\MAPS\\NBEACH\\NBEACH.IDE;1",
			"\\DATA\\MAPS\\BANK\\BANK.IDE;1",
			"\\DATA\\MAPS\\MALL\\MALL.IDE;1",
			"\\DATA\\MAPS\\YACHT\\YACHT.IDE;1",
			"\\DATA\\MAPS\\CISLAND\\CISLAND.IDE;1",
			"\\DATA\\MAPS\\CLUB\\CLUB.IDE;1",
			"\\DATA\\MAPS\\HOTEL\\HOTEL.IDE;1",
			"\\DATA\\MAPS\\LAWYERS\\LAWYERS.IDE;1",
			"\\DATA\\MAPS\\STRIPCLB\\STRIPCLB.IDE;1",
			"\\DATA\\MAPS\\AIRPORT\\AIRPORT.IDE;1",
			"\\DATA\\MAPS\\HAITI\\HAITI.IDE;1",
			"\\DATA\\MAPS\\HAITIN\\HAITIN.IDE;1",
			"\\DATA\\MAPS\\CONCERTH\\CONCERTH.IDE;1",
			"\\DATA\\MAPS\\MANSION\\MANSION.IDE;1",
			"\\DATA\\MAPS\\ISLANDSF\\ISLANDSF.IDE;1",
			"\\DATA\\MAPS\\LITTLEHA\\LITTLEHA.IPL;1",
			"\\DATA\\MAPS\\DOWNTOWN\\DOWNTOWN.IPL;1",
			"\\DATA\\MAPS\\DOWNTOWS\\DOWNTOWS.IPL;1",
			"\\DATA\\MAPS\\DOCKS\\DOCKS.IPL;1",
			"\\DATA\\MAPS\\WASHINTN\\WASHINTN.IPL;1",
			"\\DATA\\MAPS\\WASHINTS\\WASHINTS.IPL;1",
			"\\DATA\\MAPS\\OCEANDRV\\OCEANDRV.IPL;1",
			"\\DATA\\MAPS\\OCEANDN\\OCEANDN.IPL;1",
			"\\DATA\\MAPS\\GOLF\\GOLF.IPL;1",
			"\\DATA\\MAPS\\BRIDGE\\BRIDGE.IPL;1",
			"\\DATA\\MAPS\\STARISL\\STARISL.IPL;1",
			"\\DATA\\MAPS\\NBEACHBT\\NBEACHBT.IPL;1",
			"\\DATA\\MAPS\\NBEACH\\NBEACH.IPL;1",
			"\\DATA\\MAPS\\NBEACHW\\NBEACHW.IPL;1",
			"\\DATA\\MAPS\\CISLAND\\CISLAND.IPL;1",
			"\\DATA\\MAPS\\AIRPORT\\AIRPORT.IPL;1",
			"\\DATA\\MAPS\\HAITI\\HAITI.IPL;1",
			"\\DATA\\MAPS\\HAITIN\\HAITIN.IPL;1",
			"\\DATA\\MAPS\\ISLANDSF\\ISLANDSF.IPL;1",
			"\\DATA\\MAPS\\BANK\\BANK.IPL;1",
			"\\DATA\\MAPS\\MALL\\MALL.IPL;1",
			"\\DATA\\MAPS\\YACHT\\YACHT.IPL;1",
			"\\DATA\\MAPS\\CLUB\\CLUB.IPL;1",
			"\\DATA\\MAPS\\HOTEL\\HOTEL.IPL;1",
			"\\DATA\\MAPS\\LAWYERS\\LAWYERS.IPL;1",
			"\\DATA\\MAPS\\STRIPCLB\\STRIPCLB.IPL;1",
			"\\DATA\\MAPS\\CONCERTH\\CONCERTH.IPL;1",
			"\\DATA\\MAPS\\MANSION\\MANSION.IPL;1",
			"\\DATA\\MAPS\\GENERIC.IDE;1",
			"\\DATA\\OCCLU.IPL;1",
			"\\DATA\\MAPS\\PATHS.IPL;1",
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
			"\\TXD\\SPLASH1.TXD;1"
		};
		
		for ( int32 i = 0; i < ARRAY_SIZE(files); i++ )
			SkyRegisterFileOnCd([i]);
#endif
		
#ifdef GTA_PS2
		AddIntcHandler(INTC_VBLANK_S, VBlankCounter, 0);
#endif
		
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
		
		// gameAlreadyInitialised = true;	// why is this gone?
	}
}

int32 SkipAllMPEGs;
int32 gMemoryStickLoadOK;

void PlayIntroMPEGs()
{
#ifdef GTA_PS2
	if (gameAlreadyInitialised)
		RpSkySuspend();

	InitMPEGPlayer();

	float skipTime;		// wrong type, should be int
#ifdef GTA_PAL
	if(gMemoryStickLoadOK)
		skipTime = 2500000;
	else
		skipTime = 5300000;

	if(!SkipAllMPEGs)
		PlayMPEG("cdrom0:\\MOVIES\\VCPAL.PSS;1", false, unk);

	if(!SkipAllMPEGs){
		SkipAllMPEGs = true;
		PlayMPEG("cdrom0:\\MOVIES\\VICEPAL.PSS;1", true, 0);
	}
#else
	if(gMemoryStickLoadOK)
		skipTime = 2750000;
	else
		skipTime = 5500000;

	if(!SkipAllMPEGs)
		PlayMPEG("cdrom0:\\MOVIES\\VCNTSC.PSS;1", false, unk);

	if(!SkipAllMPEGs){
		SkipAllMPEGs = true;
		PlayMPEG("cdrom0:\\MOVIES\\VICE.PSS;1", true, 0);
	}
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

	if(RsEventHandler(rsINITIALIZE, nil) == rsEVENTERROR)
		return 0;

#ifdef GTA_PS2
	int32 r = TheMemoryCard.CheckCardStateAtGameStartUp(CARD_ONE);
		
	if ( r == CMemoryCard::ERR_DIRNOENTRY  || r == CMemoryCard::ERR_NOFORMAT )
	{
		GameInit(true);
		
		TheText.Unload();
		TheText.Load();
		
		CFont::Initialise();
		
		FrontEndMenuManager.DrawMemoryCardStartUpMenus();
	}else if(r == CMemoryCard::ERR_OPENNOENTRY)
		gMemoryStickLoadOK = false;
	else if(r == CMemoryCard::ERR_NONE)
		gMemoryStickLoadOK = true;
#endif

	PlayIntroMPEGs();

	GameInit(false);

	frameCount = 0;
	while(frameCount < 100);

	CGame::InitialiseOnceAfterRW();

	TheGame();

#if 0	// maybe ifndef FINAL or MASTER?
	CGame::ShutDown();
	
	RwEngineStop();
	RwEngineClose();
	RwEngineTerm();
	
#ifdef __MWERKS__
	mwExit(); // metrowerks shutdown
#endif
#endif
	return 0;
}
#endif
