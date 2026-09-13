#include "common.h"

#include "main.h"
#include "FileMgr.h"
#include "ParticleMgr.h"

cParticleSystemMgr mod_ParticleSystemManager;

const char *ParticleFilename = "PARTICLE.CFG";

// PARTICLE.CFG is commonly edited on memory-constrained ports to disable
// expensive effects.  The original loader assigned entries by line number,
// so commenting out one effect shifted every following entry onto the wrong
// particle type.  Keep the on-disk names authoritative instead: missing names
// leave their fixed slots disabled without corrupting the remaining effects.
static const char *const ParticleTypeNames[MAX_PARTICLES] = {
	"SPARK",
	"SPARK_SMALL",
	"WATER_SPARK",
	"WHEEL_DIRT",
	"SAND",
	"WHEEL_WATER",
	"BLOOD",
	"BLOOD_SMALL",
	"BLOOD_SPURT",
	"DEBRIS",
	"DEBRIS2",
	"FLYERS",
	"WATER",
	"FLAME",
	"FIREBALL",
	"GUNFLASH",
	"GUNFLASH_NOANIM",
	"GUNSMOKE",
	"GUNSMOKE2",
	"CIGARETTE_SMOKE",
	"SMOKE",
	"SMOKE_SLOWMOTION",
	"DRY_ICE",
	"TEARGAS",
	"GARAGEPAINT_SPRAY",
	"SHARD",
	"SPLASH",
	"CARFLAME",
	"STEAM",
	"STEAM2",
	"STEAM_NY",
	"STEAM_NY_SLOWMOTION",
	"GROUND_STEAM",
	"ENGINE_STEAM",
	"RAINDROP",
	"RAINDROP_SMALL",
	"RAIN_SPLASH",
	"RAIN_SPLASH_BIGGROW",
	"RAIN_SPLASHUP",
	"WATERSPRAY",
	"WATERDROP",
	"BLOODDROP",
	"EXPLOSION_MEDIUM",
	"EXPLOSION_LARGE",
	"EXPLOSION_MFAST",
	"EXPLOSION_LFAST",
	"CAR_SPLASH",
	"BOAT_SPLASH",
	"BOAT_THRUSTJET",
	"WATER_HYDRANT",
	"WATER_CANNON",
	"EXTINGUISH_STEAM",
	"PED_SPLASH",
	"PEDFOOT_DUST",
	"CAR_DUST",
	"HELI_DUST",
	"HELI_ATTACK",
	"ENGINE_SMOKE",
	"ENGINE_SMOKE2",
	"CARFLAME_SMOKE",
	"FIREBALL_SMOKE",
	"PAINT_SMOKE",
	"TREE_LEAVES",
	"CARCOLLISION_DUST",
	"CAR_DEBRIS",
	"BIRD_DEBRIS",
	"HELI_DEBRIS",
	"EXHAUST_FUMES",
	"RUBBER_SMOKE",
	"BURNINGRUBBER_SMOKE",
	"BULLETHIT_SMOKE",
	"GUNSHELL_FIRST",
	"GUNSHELL",
	"GUNSHELL_BUMP1",
	"GUNSHELL_BUMP2",
	"ROCKET_SMOKE",
	"TEST",
	"BIRD_FRONT",
	"SHIP_SIDE",
	"BEASTIE",
	"RAINDROP_2D",
	"HEATHAZE",
	"HEATHAZE_IN_DIST"
};

static int32
FindParticleType(const char *name)
{
	for (int32 i = 0; i < MAX_PARTICLES; i++)
		if (!strcmp(name, ParticleTypeNames[i]))
			return i;
	return -1;
}

cParticleSystemMgr::cParticleSystemMgr()
{
	memset(this, 0, sizeof(*this));
}

void cParticleSystemMgr::Initialise()
{
	LoadParticleData();
	
	for ( int32 i = 0; i < MAX_PARTICLES; i++ )
		m_aParticles[i].m_pParticles = nil;
}

void cParticleSystemMgr::LoadParticleData()
{
	// The original VC data has a 41-column layout.  Its tail is trail length,
	// texture stretch X/Y, wind factor, create range and flags.  Use the complete
	// VC contract here; the previous 3DS data file was a reduced 38-column file
	// whose create range and flags landed in the stretch fields.
	// ReloadConfig is also called after CParticle::Initialise has assigned each
	// system's raster table, so preserve those runtime-only pointers while
	// clearing the editable config data.  Losing them binds librw's white
	// fallback texture and turns every particle into a solid-colour rectangle.
	RwRaster **rasters[MAX_PARTICLES];
	for (int32 i = 0; i < MAX_PARTICLES; i++)
		rasters[i] = m_aParticles[i].m_ppRaster;

	memset(m_aParticles, 0, sizeof(m_aParticles));

	for (int32 i = 0; i < MAX_PARTICLES; i++)
		m_aParticles[i].m_ppRaster = rasters[i];

	CFileMgr::SetDir("DATA");
	CFileMgr::LoadFile(ParticleFilename, work_buff, ARRAY_SIZE(work_buff), "r");
	CFileMgr::SetDir("");
	
	tParticleSystemData *entry = nil;
	char *lineStart = (char *)work_buff;
	char *lineEnd = lineStart + 1;
	
	char line[500];
	char delims[4];

	while ( true )
	{
		ASSERT(lineStart != nil);
		ASSERT(lineEnd != nil);
		
		while ( *lineEnd != '\n' )
			++lineEnd;
		
		int32 lineLength = lineEnd - lineStart;
		
		ASSERT(lineLength < 500);

		strncpy(line, lineStart, lineLength);
		
		line[lineLength] = '\0';
		
		if ( !strcmp(line, ";the end") )
			break;
		
		if ( *line != ';' )
		{
			int32 param = CFG_PARAM_FIRST;

			strcpy(delims, " \t");
			
			char *value = strtok(line, delims);
			
			ASSERT(value != nil);

			int32 type = FindParticleType(value);
			if ( type < PARTICLE_FIRST || type > PARTICLE_LAST )
			{
				lineEnd++;
				lineStart = lineEnd;
				lineEnd++;
				continue;
			}
			
			do
			{	
				switch ( param )
				{
					case CFG_PARAM_PARTICLE_TYPE_NAME:
						entry = &m_aParticles[type];
						ASSERT(entry != nil);
						entry->m_Type = (tParticleType)type;
						strcpy(entry->m_aName, value);
						break;

					case CFG_PARAM_RENDER_COLOURING_R:
						entry->m_RenderColouring.red = atoi(value);
						break;

					case CFG_PARAM_RENDER_COLOURING_G:
						entry->m_RenderColouring.green = atoi(value);
						break;

					case CFG_PARAM_RENDER_COLOURING_B:
						entry->m_RenderColouring.blue = atoi(value);
						break;

					case CFG_PARAM_INITIAL_COLOR_VARIATION:
						entry->m_InitialColorVariation = Min(atoi(value), 100);
						break;

					case CFG_PARAM_FADE_DESTINATION_COLOR_R:
						entry->m_FadeDestinationColor.red = atoi(value);
						break;

					case CFG_PARAM_FADE_DESTINATION_COLOR_G:
						entry->m_FadeDestinationColor.green = atoi(value);
						break;

					case CFG_PARAM_FADE_DESTINATION_COLOR_B:
						entry->m_FadeDestinationColor.blue = atoi(value);
						break;

					case CFG_PARAM_COLOR_FADE_TIME:
						entry->m_ColorFadeTime = atoi(value);
						break;

					case CFG_PARAM_DEFAULT_INITIAL_RADIUS:
						entry->m_fDefaultInitialRadius = atof(value);
						break;

					case CFG_PARAM_EXPANSION_RATE:
						entry->m_fExpansionRate = atof(value);
						break;

					case CFG_PARAM_INITIAL_INTENSITY:
						entry->m_nFadeToBlackInitialIntensity = atoi(value);
						break;

					case CFG_PARAM_FADE_TIME:
						entry->m_nFadeToBlackTime = atoi(value);
						break;

					case CFG_PARAM_FADE_AMOUNT:
						entry->m_nFadeToBlackAmount = atoi(value);
						break;

					case CFG_PARAM_INITIAL_ALPHA_INTENSITY:
						entry->m_nFadeAlphaInitialIntensity = atoi(value);
						break;

					case CFG_PARAM_FADE_ALPHA_TIME:
						entry->m_nFadeAlphaTime = atoi(value);
						break;

					case CFG_PARAM_FADE_ALPHA_AMOUNT:
						entry->m_nFadeAlphaAmount = atoi(value);
						break;

					case CFG_PARAM_INITIAL_ANGLE:
						entry->m_nZRotationInitialAngle = atoi(value);
						break;

					case CFG_PARAM_CHANGE_TIME:
						entry->m_nZRotationChangeTime = atoi(value);
						break;

					case CFG_PARAM_ANGLE_CHANGE_AMOUNT:
						entry->m_nZRotationAngleChangeAmount = atoi(value);
						break;

					case CFG_PARAM_INITIAL_Z_RADIUS:
						entry->m_fInitialZRadius = atof(value);
						break;

					case CFG_PARAM_Z_RADIUS_CHANGE_TIME:
						entry->m_nZRadiusChangeTime = atoi(value);
						break;

					case CFG_PARAM_Z_RADIUS_CHANGE_AMOUNT:
						entry->m_fZRadiusChangeAmount = atof(value);
						break;

					case CFG_PARAM_ANIMATION_SPEED:
						entry->m_nAnimationSpeed = atoi(value);
						break;

					case CFG_PARAM_START_ANIMATION_FRAME:
						entry->m_nStartAnimationFrame = atoi(value);
						break;

					case CFG_PARAM_FINAL_ANIMATION_FRAME:
						entry->m_nFinalAnimationFrame = atoi(value);
						break;

					case CFG_PARAM_ROTATION_SPEED:
						entry->m_nRotationSpeed = atoi(value);
						break;

					case CFG_PARAM_GRAVITATIONAL_ACCELERATION:
						entry->m_fGravitationalAcceleration = atof(value);
						break;

					case CFG_PARAM_FRICTION_DECCELERATION:
						entry->m_nFrictionDecceleration = atoi(value);
						break;

					case CFG_PARAM_LIFE_SPAN:
						entry->m_nLifeSpan = atoi(value);
						break;

					case CFG_PARAM_POSITION_RANDOM_ERROR:
						entry->m_fPositionRandomError = atof(value);
						break;

					case CFG_PARAM_VELOCITY_RANDOM_ERROR:
						entry->m_fVelocityRandomError = atof(value);
						break;

					case CFG_PARAM_EXPANSION_RATE_ERROR:
						entry->m_fExpansionRateError = atof(value);
						break;

					case CFG_PARAM_ROTATION_RATE_ERROR:
						entry->m_nRotationRateError = atoi(value);
						break;

					case CFG_PARAM_LIFE_SPAN_ERROR_SHAPE:
						entry->m_nLifeSpanErrorShape = atoi(value);
						break;

					case CFG_PARAM_TRAIL_LENGTH_MULTIPLIER:
						entry->m_fTrailLengthMultiplier = atof(value);
						break;

					case CFG_PARAM_STRETCH_VALUE_X:
						entry->m_vecTextureStretch.x = atof(value);
						break;

					case CFG_PARAM_STRETCH_VALUE_Y:
						entry->m_vecTextureStretch.y = atof(value);
						break;

					case CFG_PARAM_WIND_FACTOR:
						entry->m_fWindFactor = atof(value);
						break;

					case CFG_PARAM_PARTICLE_CREATE_RANGE:
						entry->m_fCreateRange = SQR(atof(value));
						break;

					case CFG_PARAM_FLAGS:
						entry->Flags = atoi(value);
						break;
				}
				
				value = strtok(nil, delims);

				param++;

				if ( param > CFG_PARAM_LAST )
					param = CFG_PARAM_FIRST;
		
			} while ( value != nil );
		}
		
		lineEnd++;
		lineStart = lineEnd;
		lineEnd++;
	}
}
