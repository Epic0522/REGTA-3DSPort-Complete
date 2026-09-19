#include "common.h"

#ifdef ENABLE_3DS_BOTTOM_RADAR

#include "lodepng/lodepng.h"
#include "bottom_menu_map_bin.h"
#include "bottom_touch_bin.h"

#include "BottomScreen3DS.h"
#include "Camera.h"
#include "Clock.h"
#include "CutsceneMgr.h"
#include "Frontend.h"
#include "Font.h"
#include "Hud.h"
#include "ModelInfo.h"
#include "Pad.h"
#include "PlayerPed.h"
#include "Radar.h"
#include "RwHelper.h"
#include "Sprite2d.h"
#include "TxdStore.h"
#include "WeaponInfo.h"
#include "World.h"

namespace {
static RwCamera *BottomCamera;
static RwTexture *BottomMenuMapTexture;
enum { NUM_BOTTOM_TOUCH_PARTS = 4 };
static RwTexture *BottomTouchOverlayTextures[NUM_BOTTOM_TOUCH_PARTS];

struct BottomTouchOverlayPart {
	int sourceX, sourceY;
	int width, height;
	int textureWidth, textureHeight;
};

static const BottomTouchOverlayPart BottomTouchOverlayParts[NUM_BOTTOM_TOUCH_PARTS] = {
	{ 23,  26, 125,  79, 128, 128 },
	{ 173, 26, 125,  79, 128, 128 },
	{ 28, 120, 256, 102, 256, 128 },
	{ 284,120,   8, 102,   8, 128 }
};

static const CRGBA LCS_THEME(0xC4, 0x37, 0x39, 255);

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
	const int32 stride = RwImageGetStride(image);
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
	if(BottomTouchOverlayTextures[0])
		return true;
	unsigned char *rgba = nil;
	unsigned width = 0, height = 0;
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
DrawBottomTouchOverlay(void)
{
	if(!CPad::Is3DSTouchOverlayVisible() || BottomTouchOverlayTextures[0] == nil)
		return;
	for(int i = 0; i < NUM_BOTTOM_TOUCH_PARTS; i++) {
		const BottomTouchOverlayPart &part = BottomTouchOverlayParts[i];
		CSprite2d overlay;
		overlay.m_pTexture = BottomTouchOverlayTextures[i];
		const float maxU = (float)part.width / part.textureWidth;
		const float maxV = (float)part.height / part.textureHeight;
		overlay.Draw(CRect((float)part.sourceX, (float)part.sourceY,
			(float)(part.sourceX + part.width), (float)(part.sourceY + part.height)),
			CRGBA(0xF2, 0xB0, 0x5C, 255),
			0.0f, 0.0f, maxU, 0.0f,
			0.0f, maxV, maxU, maxV);
		overlay.m_pTexture = nil;
	}
}

static RwTexture *
CreateMenuMapTexture(void)
{
	unsigned char *rgba = nil;
	unsigned width = 0, height = 0;
	if(lodepng_decode32(&rgba, &width, &height,
		bottom_menu_map_bin, bottom_menu_map_bin_size) != 0 ||
		width != 320 || height != 220) {
		free(rgba);
		return nil;
	}
	RwImage *image = RwImageCreate(512, 256, 32);
	if(image == nil || RwImageAllocatePixels(image) == nil) {
		if(image) RwImageDestroy(image);
		free(rgba);
		return nil;
	}
	uint8 *pixels = RwImageGetPixels(image);
	const int32 stride = RwImageGetStride(image);
	memset(pixels, 0, stride * 256);
	for(int y = 0; y < 220; y++)
		memcpy(pixels + y * stride, rgba + y * 320 * 4, 320 * 4);
	free(rgba);
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

static RwCamera *
CreateBottomCamera(void)
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
DrawMenuMap(void)
{
	CSprite2d::DrawRect(CRect(0.0f, 0.0f, 320.0f, 240.0f), CRGBA(0, 0, 0, 255));
	// The source has a little black headroom.  Match the re3/reVC menu-map
	// placement by moving the artwork up while retaining its aspect ratio.
	DrawLCSMenuMap3DS(CRect(LCS_BOTTOM_MAP_LEFT, LCS_BOTTOM_MAP_TOP,
		LCS_BOTTOM_MAP_RIGHT, LCS_BOTTOM_MAP_BOTTOM));
}

static void
DrawHudNumberString(const char *str, float x, float y, bool secondSet,
	const CRGBA &colour, float width = 11.0f, float height = 10.0f)
{
	/* LCS' heading font is much wider than VC's and cannot be placed with the
	 * reVC font metrics.  Use LCS' own thin HUD-number atlas in the same 80px
	 * rail instead; this is also what the stock LCS HUD uses for time/cash. */
	while(*str) {
		uint8 c = *str++;
		if(c >= '0' && c <= ':')
			c = secondSet ? c - '%' : c - '0';
		else
			c = secondSet ? 21 : 10;
		int row = c / 8;
		int col = c - row * 8;
		float u = col * 0.125f;
		float v = row * 0.265625f;
		CHud::Sprites[HUD_HUDNUMBERS].Draw(CRect(x, y, x + width, y + height),
			colour, u, v, u + 0.125f, v,
			u, v + 0.265625f, u + 0.125f, v + 0.265625f);
		x += width - 2.0f;
		if(c == 10)
			x -= 4.0f;
	}
}

static void
DrawAmmoString(const char *ascii)
{
	wchar text[32];
	AsciiToUnicode(ascii, text);
	CFont::SetBackgroundOff();
	CFont::SetJustifyOff();
	CFont::SetRightJustifyOff();
	CFont::SetCentreOn();
	CFont::SetCentreSize(76.0f);
	CFont::SetPropOn();
	CFont::SetFontStyle(FONT_STANDARD);
	CFont::SetDropShadowPosition(2);
	CFont::SetDropColor(CRGBA(0, 0, 0, 255));
	CFont::SetScale(0.40f, 0.62f);
	CFont::SetColor(CRGBA(255, 255, 255, 255));
	CFont::PrintString(280.0f, 74.0f, text);
}

static void
DrawHudMeter(float value, float maximum, float y, int fillSprite, int darkSprite,
	bool ghostWhenEmpty)
{
	/* Keep the native LCS bar textures, enlarged to the visual weight of the
	 * stock HUD.  Their useful pixels occupy x=20..60 of a 64-pixel texture,
	 * so centre that visible portion rather than the transparent canvas. */
	const float x = 238.5f;
	const float width = 68.0f;
	const float height = 15.0f;
	float fraction = maximum > 0.0f ? Clamp(value / maximum, 0.0f, 1.0f) : 0.0f;
	float u = fraction <= 0.0f ? 0.0f :
		(fraction >= 1.0f ? 1.0f : (fraction * 40.0f + 20.0f) / 64.0f);
	const uint8 alpha = ghostWhenEmpty && value <= 1.0f ? 82 : 255;
	const CRGBA white(255, 255, 255, alpha);
	if(u > 0.0f)
		CHud::Sprites[fillSprite].Draw(CRect(x, y, x + width * u, y + height),
			white, 0.0f, 0.0f, u, 0.0f, 0.0f, 1.0f, u, 1.0f);
	if(u < 1.0f)
		CHud::Sprites[darkSprite].Draw(CRect(x + width * u, y, x + width, y + height),
			white, u, 0.0f, 1.0f, 0.0f, u, 1.0f, 1.0f, 1.0f);
	CHud::Sprites[HUD_BAR_OUTLINE].Draw(CRect(x, y, x + width, y + height),
		white, 0.01f, 0.0f, 1.0f, 0.0f, 0.01f, 1.0f, 1.0f, 1.0f);
}

static void
DrawHudBoostPlus(float y)
{
	/* Meter sprites are buffered, while DrawRect is immediate.  Flush the meter
	 * first so it cannot later cover one arm of the plus (which made health and
	 * armour appear to use different glyphs).  Use integer-aligned, identical
	 * 15x15 geometry at the same offset from each meter's top-left corner. */
	CSprite2d::RenderVertexBuffer();
	const float left = 253.0f;
	const float top = y - 6.0f;
	const CRGBA outline(18, 18, 18, 240);
	const CRGBA white(245, 245, 245, 255);
	CSprite2d::DrawRect(CRect(left, top + 4.0f,
		left + 15.0f, top + 11.0f), outline);
	CSprite2d::DrawRect(CRect(left + 4.0f, top,
		left + 11.0f, top + 15.0f), outline);
	CSprite2d::DrawRect(CRect(left + 1.0f, top + 5.0f,
		left + 14.0f, top + 10.0f), white);
	CSprite2d::DrawRect(CRect(left + 5.0f, top + 1.0f,
		left + 10.0f, top + 14.0f), white);
}

static void
DrawStatusBar(CPlayerPed *player)
{
	/* The radar already spans all 320 pixels with its centre at x=120.  The
	 * status rail uses reVC's production blend value, so the map remains visible
	 * beneath it; LCS identity comes from the red rule and HUD assets. */
	CSprite2d::DrawRect(CRect(240.0f, 0.0f, 320.0f, 240.0f),
		CRGBA(8, 10, 16, 145));
	CSprite2d::DrawRect(CRect(240.0f, 0.0f, 242.0f, 240.0f),
		CRGBA(LCS_THEME.r, LCS_THEME.g, LCS_THEME.b, 210));
	if(player == nil)
		return;

	CWeapon *weapon = player->GetWeapon();
	int weaponType = weapon->m_eWeaponType;
	CWeaponInfo *weaponInfo = CWeaponInfo::GetWeaponInfo((eWeaponType)weaponType);
	// LCS HUD icons have nine transparent pixels on the visual right side of
	// the reVC-sized slot. Shift the source asset, not the shared HUD layout.
	const CRect iconRect(259.0f, 13.0f, 319.0f, 71.0f);
	if(weaponInfo->m_nModelId <= 0) {
		if(weaponType >= 0 && weaponType < NUM_HUD_SPRITES)
			CHud::Sprites[weaponType].Draw(iconRect,
				CRGBA(255, 255, 255, 255));
	} else {
		CBaseModelInfo *model = CModelInfo::GetModelInfo(weaponInfo->m_nModelId);
		if(model) {
			RwTexDictionary *txd = CTxdStore::GetSlot(model->GetTxdSlot())->texDict;
			RwTexture *texture = txd ?
				RwTexDictionaryFindNamedTexture(txd, model->GetModelName()) : nil;
			if(texture) {
				static CSprite2d icon;
				icon.m_pTexture = texture;
				icon.Draw(iconRect,
					CRGBA(255, 255, 255, 255));
				icon.m_pTexture = nil;
			}
		}
	}

	char ascii[32];
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
		DrawAmmoString(ascii);
	}
	sprintf(ascii, "%02d:%02d", CClock::GetHours(), CClock::GetMinutes());
	/* The two halves of LCS' HUD-number atlas already contain the stock gold
	 * clock and green cash colours.  White modulation preserves those colours. */
	DrawHudNumberString(ascii, 252.0f, 101.0f, false,
		CRGBA(255, 255, 255, 255), 14.0f, 14.0f);
	sprintf(ascii, "$%07d", CWorld::Players[CWorld::PlayerInFocus].m_nVisibleMoney);
	DrawHudNumberString(ascii, 246.0f, 138.0f, true,
		CRGBA(255, 255, 255, 255));
	DrawHudMeter(player->m_fHealth,
		CWorld::Players[CWorld::PlayerInFocus].m_nMaxHealth,
		168.0f, HUD_BAR_INSIDE2, HUD_BAR_INSIDE2DARK, false);
	if(CWorld::Players[CWorld::PlayerInFocus].m_nMaxHealth > 100)
		DrawHudBoostPlus(168.0f);
	DrawHudMeter(player->m_fArmour,
		CWorld::Players[CWorld::PlayerInFocus].m_nMaxArmour,
		201.0f, HUD_BAR_INSIDE1, HUD_BAR_INSIDE1DARK, true);
	if(CWorld::Players[CWorld::PlayerInFocus].m_nMaxArmour > 100)
		DrawHudBoostPlus(201.0f);
	CFont::DrawFonts();
}
}

void
InitialiseBottomScreen(void)
{
	/* Match re3/reVC: reserve these small textures before world textures fill
	 * linear memory, rather than allocating them on the first touch. */
	CreateBottomTouchOverlayTextures();
}

bool
DrawLCSMenuMap3DS(const CRect &rect)
{
	if(BottomMenuMapTexture == nil)
		BottomMenuMapTexture = CreateMenuMapTexture();
	if(BottomMenuMapTexture == nil)
		return false;
	CSprite2d map;
	map.m_pTexture = BottomMenuMapTexture;
	map.Draw(rect, CRGBA(255, 255, 255, 255),
		0.0f, 0.0f, 320.0f / 512.0f, 0.0f,
		0.0f, 220.0f / 256.0f, 320.0f / 512.0f, 220.0f / 256.0f);
	map.m_pTexture = nil;
	return true;
}

void
RenderBottomScreen(void)
{
	const bool coldStartMenu = FrontEndMenuManager.m_bGameNotLoaded &&
		FrontEndMenuManager.m_bMenuActive;
	if(!coldStartMenu && (FrontEndMenuManager.m_bGameNotLoaded ||
		FrontEndMenuManager.m_bMenuActive))
		return; // Pause preserves the most recent gameplay radar frame.
	if(BottomCamera == nil)
		BottomCamera = CreateBottomCamera();
	if(BottomMenuMapTexture == nil)
		BottomMenuMapTexture = CreateMenuMapTexture();
	if(BottomCamera == nil)
		return;
	CRGBA clear(0, 0, 0, 255);
	RwCameraClear(BottomCamera, &clear.rwRGBA,
		rwCAMERACLEARIMAGE | rwCAMERACLEARZ | rwCAMERACLEARSTENCIL);
	if(!RwCameraBeginUpdate(BottomCamera))
		return;
	CSprite2d::InitPerFrame();
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);

	if(coldStartMenu) {
		DrawMenuMap();
	} else if(!TheCamera.m_WideScreenOn && !CCutsceneMgr::IsCutsceneProcessing() &&
		CHud::m_Wants_To_Draw_Hud) {
		CPlayerPed *player = FindPlayerPed();
		if(player) {
			CRadar::m_bDrawingBottomScreen = true;
			if(FrontEndMenuManager.m_PrefsRadarMode != 2) {
				CRadar::DrawMap();
				CRadar::DrawBlips();
			}
			CRadar::m_bDrawingBottomScreen = false;
			/* Radar rendering disables vertex alpha.  Restore the exact reVC
			 * composition state before drawing the translucent status rail; without
			 * this, its alpha is ignored and it hides the map at x=240. */
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
			DrawStatusBar(player);
			DrawBottomTouchOverlay();
		}
	}

	RwCameraEndUpdate(BottomCamera);
	RwCameraShowRaster(BottomCamera, nil, rwRASTERFLIPDONTWAIT);
}

void
ShutdownBottomScreen(void)
{
	if(BottomMenuMapTexture) {
		RwTextureDestroy(BottomMenuMapTexture);
		BottomMenuMapTexture = nil;
	}
	for(int i = 0; i < NUM_BOTTOM_TOUCH_PARTS; i++) {
		if(BottomTouchOverlayTextures[i]) {
			RwTextureDestroy(BottomTouchOverlayTextures[i]);
			BottomTouchOverlayTextures[i] = nil;
		}
	}
	if(BottomCamera) {
		CameraDestroy(BottomCamera);
		BottomCamera = nil;
	}
}

#endif
