#include "common.h"

#include "WeaponEffects.h"
#include "TxdStore.h"
#include "Sprite.h"

RwTexture *gpCrossHairTex;
RwRaster *gpCrossHairRaster;

CWeaponEffects gCrossHair;

CWeaponEffects::CWeaponEffects()
{
	
}

CWeaponEffects::~CWeaponEffects()
{
	
}

void
CWeaponEffects::Init(void)
{
	gCrossHair.m_bActive = false;
	gCrossHair.m_vecPos = CVector(0.0f, 0.0f, 0.0f);
	gCrossHair.m_nRed = 0;
	gCrossHair.m_nGreen = 0;
	gCrossHair.m_nBlue = 0;
	gCrossHair.m_nAlpha = 255;
	gCrossHair.m_fSize = 1.0f;
	gCrossHair.m_fRotation = 0.0f;
	
	
	CTxdStore::PushCurrentTxd();
	int32 slot = CTxdStore::FindTxdSlot("particle");
	CTxdStore::SetCurrentTxd(slot);
	
	gpCrossHairTex    = RwTextureRead("crosshair", nil);
	gpCrossHairRaster = RwTextureGetRaster(gpCrossHairTex);
	
	CTxdStore::PopCurrentTxd();
}

void
CWeaponEffects::Shutdown(void)
{
	RwTextureDestroy(gpCrossHairTex);
#if GTA_VERSION >= GTA3_PC_11
	gpCrossHairTex = nil;
#endif
}

void
CWeaponEffects::MarkTarget(CVector pos, uint8 red, uint8 green, uint8 blue, uint8 alpha, float size)
{
	gCrossHair.m_bActive = true;
	gCrossHair.m_vecPos = pos;
	/* Controller lock-on passes dark 64-level colours which only worked with
	 * the original additive glow.  Normalize the hue to a solid, readable
	 * colour for the 3DS screen. */
	uint8 peak = Max(red, Max(green, blue));
	if(peak != 0){
		gCrossHair.m_nRed = red * 255 / peak;
		gCrossHair.m_nGreen = green * 255 / peak;
		gCrossHair.m_nBlue = blue * 255 / peak;
	}else{
		gCrossHair.m_nRed = 255;
		gCrossHair.m_nGreen = 48;
		gCrossHair.m_nBlue = 48;
	}
	gCrossHair.m_nAlpha = 255;
	gCrossHair.m_fSize = size;
}

void
CWeaponEffects::ClearCrossHair(void)
{
	gCrossHair.m_bActive = false;
}

void
CWeaponEffects::Render(void)
{
	if ( gCrossHair.m_bActive )
	{
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE,      (void *)FALSE);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void *)TRUE);
		/* The stock crosshair has opaque black texels and relies on additive
		 * blending to make the background invisible. */
		RwRenderStateSet(rwRENDERSTATESRCBLEND,          (void *)rwBLENDONE);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND,         (void *)rwBLENDONE);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER,     (void *)gpCrossHairRaster);

		RwV3d pos;
		float w, h;
		if ( CSprite::CalcScreenCoors(gCrossHair.m_vecPos, &pos, &w, &h, true) )
		{
			float recipz = 1.0f / pos.z;
			/* Dilate the one-pixel stock outline to a 3x3 physical-pixel footprint.
			 * The black texture background remains invisible under additive blend. */
			for(int32 y = -1; y <= 1; y++)
				for(int32 x = -1; x <= 1; x++)
					CSprite::RenderOneXLUSprite(pos.x + x, pos.y + y, pos.z,
						gCrossHair.m_fSize * w, gCrossHair.m_fSize * h,
						gCrossHair.m_nRed, gCrossHair.m_nGreen, gCrossHair.m_nBlue, 256,
						recipz, gCrossHair.m_nAlpha);
		}
		
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void *)FALSE);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE,      (void *)FALSE);
		RwRenderStateSet(rwRENDERSTATESRCBLEND,          (void *)rwBLENDSRCALPHA);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND,         (void *)rwBLENDINVSRCALPHA);
	}
}
