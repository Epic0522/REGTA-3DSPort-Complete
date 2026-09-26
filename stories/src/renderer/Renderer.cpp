#define WITHD3D
#include "common.h"
#include "../../../../common/3ds/CameraPresetTransition.h"
#ifdef _3DS
#include "../../../../common/3ds/WorldDrawDistance.h"
#include <map>
#include <vector>
#endif

#include "main.h"
#include "General.h"
#include "Lights.h"
#include "ModelInfo.h"
#include "Treadable.h"
#include "Ped.h"
#include "Vehicle.h"
#include "Boat.h"
#include "Heli.h"
#include "Bike.h"
#include "Object.h"
#include "PathFind.h"
#include "Collision.h"
#include "VisibilityPlugins.h"
#include "2dEffect.h"
#ifdef _3DS
#include "../../../../common/3ds/CrowdDrawBudget.h"
#include "../../../../common/3ds/CameraOcclusionRender.h"
#endif
#include "Clock.h"
#include "World.h"
#include "Camera.h"
#include "ModelIndices.h"
#include "Streaming.h"
#include "Shadows.h"
#ifdef _3DS
#include "Skidmarks.h"
#include "Timer.h"
#endif
#include "Coronas.h"
#include "PointLights.h"
#include "Occlusion.h"
#include "Renderer.h"
#include "RwHelper.h"
#include "custompipes.h"
#include "Frontend.h"
#include "Ferry.h"
#include "Plane.h"
#include "WaterLevel.h"
#include "PlayerInfo.h"

// maybe some day...
//#define GTA_WORLDSTREAM

bool gbShowPedRoadGroups;
bool gbShowCarRoadGroups;
bool gbShowCollisionPolys;
bool gbShowCollisionPolysReflections;
bool gbShowCollisionPolysNoShadows;
bool gbShowCollisionLines;
bool gbBigWhiteDebugLightSwitchedOn;

bool gbDontRenderBuildings;
bool gbDontRenderBigBuildings;
bool gbDontRenderPeds;
bool gbDontRenderObjects;
bool gbDontRenderVehicles;

bool gbRenderDebugEnvMap;

// unused
bool gbLighting;

// unused
int16 TestCloseThings;
int16 TestBigThings;

struct EntityInfo
{
	CEntity *ent;
	float sort;
};

CLinkList<EntityInfo> gSortedVehiclesAndPeds;

int32 CRenderer::ms_nNoOfVisibleEntities;
CEntity *CRenderer::ms_aVisibleEntityPtrs[NUMVISIBLEENTITIES];
CEntity *CRenderer::ms_aInVisibleEntityPtrs[NUMINVISIBLEENTITIES];
int32 CRenderer::ms_nNoOfInVisibleEntities;
int32 CRenderer::ms_nNoOfVisibleVehicles;
CEntity *CRenderer::ms_aVisibleVehiclePtrs[NUMVISIBLEENTITIES];
#ifdef NEW_RENDERER
int32 CRenderer::ms_nNoOfVisibleBuildings;
CEntity *CRenderer::ms_aVisibleBuildingPtrs[NUMVISIBLEENTITIES];
#endif

CVector CRenderer::ms_vecCameraPosition;
CVehicle *CRenderer::m_pFirstPersonVehicle;
bool CRenderer::m_loadingPriority;
#ifdef _3DS
float CRenderer::ms_lodDistScale = 0.65f;
#else
float CRenderer::ms_lodDistScale = 1.2f;
#endif

#ifdef _3DS
static bool
IsNew3DSVegetationModel(CSimpleModelInfo *mi, int16 modelId)
{
	(void)mi;
	return IsLCSVegetationModel(modelId);
}

static bool
IsNew3DSSmallStreetProp(int16 modelId)
{
	/* These are the frequently repeated street details whose authored draw
	 * distances became visibly abrupt after the general world was reduced to
	 * 0.55.  Keep the exception semantic rather than using broad ID ranges so
	 * buildings and expensive one-off scenery retain the existing budget. */
	return IsLampPost(modelId) || IsTrafficLight(modelId) || IsLCSTrafficLight(modelId) ||
		modelId == MI_WASTEBIN || modelId == MI_BIN || modelId == MI_DUMP1 ||
		modelId == MI_POSTBOX1 || modelId == MI_NEWSSTAND ||
		modelId == MI_BUSSIGN1 || modelId == MI_NOPARKINGSIGN1 ||
		modelId == MI_PHONESIGN || modelId == MI_TAXISIGN ||
		modelId == MI_PHONEBOOTH1;
}

static bool
IsLCSCallahanPhaseModel(int16 modelId)
{
	/* These few authored bridge-phase pieces must be visible before the player
	 * reaches the jump.  Keeping them at source distance is cheap and avoids
	 * increasing the budget for every city building. */
	return modelId == MI_IN_BMBRIDRAMP3 || modelId == MI_IN_BMBRIDGE2 ||
		modelId == MI_BM_LIGHTRIG3 || modelId == MI_IN_BMSCAFF_UPS ||
		modelId == MI_IN_BMSCAFFH_NS || modelId == MI_IN_BM_SCAFFCOVR ||
		modelId == MI_IN_BM_SCAFFH_WE || modelId == MI_IN_PMBRIDRAMP3 ||
		modelId == MI_IN_PMBRIDGE2 || modelId == MI_PM_LIGHTRIG3;
}

static bool
IsLCSIslandLodModel(int16 modelId)
{
	/* These five models are complete low-detail islands rather than ordinary
	 * large city blocks.  Their authored origin distance and 2-3.5 km draw
	 * ranges are part of the cross-island visibility scheme. */
	return modelId == islandLODindust || modelId == islandLODcomInd ||
		modelId == islandLODcomSub || modelId == islandLODsubInd ||
		modelId == islandLODsubCom;
}

// Model metadata is fixed after level loading. Build the reverse HD->LOD link
// once at renderer init; never scan the model table during frame rendering.
static std::vector<uint8> gLCSBuildingLodPairs;
static std::vector<CEntity*> gLCSVisibleBuildingLodSources;
static const uint8 LCS_HD_HAS_LOD = 1, LCS_LOD_HAS_HD = 2;
static const float LCS_BUILDING_HD_RANGE = 0.7f;

static void
InitLCSBuildingLodPairs(void)
{
	const int count = CModelInfo::GetNumModelInfos();
	gLCSBuildingLodPairs.assign(count, 0);
	std::map<CSimpleModelInfo*, int> ids;
	for(int id = 0; id < count; ++id){
		CBaseModelInfo *base = CModelInfo::GetModelInfo(id);
		if(base && base->IsSimple())
			ids[(CSimpleModelInfo*)base] = id;
	}
	for(const auto &entry : ids){
		CSimpleModelInfo *low = entry.first;
		if(!low->m_isBigBuilding || !low->GetRelatedModel())
			continue;
		auto highEntry = ids.find(low->GetRelatedModel());
		if(highEntry == ids.end() || highEntry->first->m_isBigBuilding)
			continue;
		bool eligible = true;
		for(int id : {entry.second, highEntry->second}){
			CSimpleModelInfo *mi = (CSimpleModelInfo*)CModelInfo::GetModelInfo(id);
			if(mi->m_wetRoadReflection || IsLCSIslandLodModel(id) ||
			   IsLCSCallahanPhaseModel(id) || IsNew3DSVegetationModel(mi, id) ||
			   IsNew3DSSmallStreetProp(id))
				eligible = false;
		}
		if(eligible){
			gLCSBuildingLodPairs[highEntry->second] |= LCS_HD_HAS_LOD;
			gLCSBuildingLodPairs[entry.second] |= LCS_LOD_HAS_HD;
		}
	}
}

static bool
HasLCSBuildingLodPair(int modelId, uint8 role)
{
	return modelId >= 0 && (size_t)modelId < gLCSBuildingLodPairs.size() &&
		(gLCSBuildingLodPairs[modelId] & role) != 0;
}

static bool
HasLCSAdaptiveLodFallback(CSimpleModelInfo *mi, int16 modelId, CEntity *ent)
{
	/* LCS legitimately contains long-range buildings with no related low model.
	 * Contracting those models' only draw distance removes the whole building.
	 * Only adapt a detailed model when another atomic, or an external paired LOD,
	 * can actually replace it. Damage atomics are not distance LODs. */
	if(ent && ent->IsBuilding() && !ent->bIsBIGBuilding &&
	   HasLCSBuildingLodPair(modelId, LCS_HD_HAS_LOD))
		return true;
	return ent && !ent->bIsBIGBuilding && mi->m_firstDamaged == 0 && mi->m_numAtomics > 1;
}

static bool
HasLCSShadowCastingLight(CSimpleModelInfo *mi)
{
	for(int i = 0; i < mi->GetNum2dEffects(); ++i){
		C2dEffect *effect = mi->Get2dEffect(i);
		if(effect && effect->type == EFFECT_LIGHT && effect->light.range > 0.0f)
			return true;
	}
	return false;
}

static void
MarkLCSVisibleBuildingLodSource(CEntity *ent)
{
	if(ent && HasLCSBuildingLodPair(ent->GetModelIndex(), LCS_HD_HAS_LOD))
		gLCSVisibleBuildingLodSources.push_back(ent);
}

static bool
HasVisibleLCSBuildingLodSource(CEntity *lod, CSimpleModelInfo *high)
{
	/* Model residency is global and says nothing about whether this particular
	 * map instance survived the current LOD test. Match the detailed entity that
	 * sector scanning actually accepted before allowing its coarse carrier out. */
	for(CEntity *candidate : gLCSVisibleBuildingLodSources){
		if(CModelInfo::GetModelInfo(candidate->GetModelIndex()) != high)
			continue;
		const CVector delta = candidate->GetPosition() - lod->GetPosition();
		/* Related LOD and detail instances often use different pivots. Phil's
		 * shop differs vertically by more than ten metres, while terrain and
		 * airport pairs can also shift their XY origins. Compare their actual
		 * world bounds; retain the tight XY fallback for old assets whose
		 * collision bounds do not represent the rendered shell. */
		if(lod->GetIsTouching(candidate) || delta.x*delta.x + delta.y*delta.y < 4.0f)
			return true;
	}
	return false;
}

// Reuse bounds only within one visibility decision: no stale cross-frame cache.
struct LCSWorldBounds {
	CVector offset;
	float radius, distance;
	LCSWorldBounds(CEntity *ent, const CVector &camera) : offset(0.0f, 0.0f, 0.0f), radius(0.0f), distance(0.0f)
	{
		if(ent){
			offset = ent->GetBoundCentre() - camera;
			radius = ent->GetBoundRadius();
			distance = offset.Magnitude();
		}
	}
};

static float
GetLCSWorldLodScale(CSimpleModelInfo *mi, int16 modelId, CEntity *ent, const LCSWorldBounds &bounds)
{
	/* Formal cutscenes preload their authored scene before playback.  Expanding
	 * the world draw radius here does not improve loading; it only makes the
	 * high-detail cutscene characters compete with much more background geometry. */
	const bool stereo = rw::c3d::stereoControlsActive();
	/* Light carriers use the same residency decision as Flat. Their 2d effect
	 * registers the point light that produces the vehicle's lamp shadow; using
	 * the shorter Stereo LOD here removes that light while headlights survive. */
	const bool adaptiveStereo = stereo && !HasLCSShadowCastingLight(mi);
	const bool performance = rw::c3d::performanceModeActive();
	const bool vegetation = IsNew3DSVegetationModel(mi, modelId);
	const bool streetProp = IsNew3DSSmallStreetProp(modelId);
	if(IsLCSIslandLodModel(modelId))
		return 1.0f;
	if(IsLCSCallahanPhaseModel(modelId))
		return 1.0f;
	/* Single-tier assets have nothing to hand off to. Preserve their authored
	 * range exactly, including buildings, lamps, signs and one-piece trees. */
	if(streetProp || !HasLCSAdaptiveLodFallback(mi, modelId, ent))
		return 1.0f;
	float scale;
	if(vegetation)
		scale = performance ? (adaptiveStereo ? 0.55f : 0.75f) : (adaptiveStereo ? 0.80f : 1.2f);
	else
		// Undo the recent performance-profile HD range increase, without
		// shortening the coarse skyline used beyond the detailed buildings.
		scale = adaptiveStereo ? (performance && (!ent || !ent->bIsBIGBuilding) ? 0.40f : 0.57f) :
			(performance ? 0.45f : 0.65f);
	if(ent){
		const CVector &offset = bounds.offset;
		const float distance = bounds.distance;
		const float baseScale = vegetation ? 1.2f : (streetProp ? 0.75f : 0.65f);
		const bool detailedBuilding = ent->IsBuilding() && !vegetation &&
			!streetProp;
		const float frontScale = detailedBuilding ? scale :
			(adaptiveStereo ? (bounds.radius < 12.0f ? (performance ? 0.70f : 0.85f) : 0.55f) : baseScale);
		scale = WorldDrawDistance3DS::WorldFrontScale(scale, Min(baseScale, frontScale),
			distance, DotProduct(offset, TheCamera.GetForward()), bounds.radius,
			adaptiveStereo || performance);
	}
	scale *= CrowdDrawBudget3DS::DetailFactor(ent, mi->m_noFade);
	// Only paired HD buildings change range. Single-model props keep their
	// original draw distance, and coarse models keep their original far limit.
	if(ent && ent->IsBuilding() && !ent->bIsBIGBuilding &&
	   HasLCSBuildingLodPair(modelId, LCS_HD_HAS_LOD))
		scale *= LCS_BUILDING_HD_RANGE;
	return scale;
}

static float
GetLCSWorldDistance(CEntity *ent, float originDistance, const LCSWorldBounds &bounds)
{
	// Preserve authored origin distance for whole-island LODs.
	if(!IsLCSIslandLodModel(ent->GetModelIndex()) && ent->IsBuilding() && bounds.radius >= 16.0f)
		return Max(bounds.distance - bounds.radius, 0.0f);
	return originDistance;
}

float
CRenderer::GetNew3DSWorldLodScale(CSimpleModelInfo *mi, int16 modelId, CEntity *ent)
{
	const LCSWorldBounds bounds(ent, ms_vecCameraPosition);
	return GetLCSWorldLodScale(mi, modelId, ent, bounds);
}

float
CRenderer::GetNew3DSWorldDistance(CEntity *ent, float originDistance)
{
	const LCSWorldBounds bounds(ent, ms_vecCameraPosition);
	return GetLCSWorldDistance(ent, originDistance, bounds);
}

namespace {
struct LCSAttachedWindowNames
{
	const char *light;
	const char *carriers[4][2];
	uint8 numCarriers;
};

struct LCSAttachedWindowModels
{
	int16 light;
	int16 carriers[4][2];
	uint8 numCarriers;
};

static const LCSAttachedWindowNames kLCSAttachedWindows[] = {
	{ "lc_inwindows11",  {{"ind_mainten2", "LOD_mainten2"}, {"iten_alleygun", "LODn_alleygun"}, {"indbigbuild", "LODbigbuild"}, {"iten_block06", "LODn_block06"}}, 4 },
	{ "lc_indwindows1",  {{"ind_newbuilds06", "LOD_newbuilds06"}, {"ind_newbuilds07", "LOD_newbuilds07"}}, 2 },
	{ "lc_indwindows2",  {{"redlightbuild03", "LODlightbuild03"}}, 1 },
	{ "lc_indwindows3",  {{"indhibuild2", "LODhibuild2"}}, 1 },
	{ "lc_indwindows4",  {{"redlightbuild08a", "LODlightbuild08a"}, {"redlightbuild12", "LODlightbuild12"}}, 2 },
	{ "lc_indwindows5",  {{"redlightbuild10", "LODlightbuild10"}}, 1 },
	{ "lc_indwindows6",  {{"indhibuild3", "LODhibuild3"}}, 1 },
	{ "lc_indwindows7",  {{"redlightbuild06v", "LODlightbuild06v"}, {"chtwn_fmrkt", "LODwn_fmrkt"}}, 2 },
	{ "lc_indwindows8",  {{"Chinabuildnew1", "LODnabuildnew1"}}, 1 },
	{ "lc_indwindows9",  {{"newbuildind", "LODbuildind"}}, 1 },
	{ "lc_indwindows10", {{"block_maindrag", "LODck_maindrag"}, {"Police_Station_ind", "LODice_Station_ind"}}, 2 },
	{ "lc_indwindows11", {{"chinaquadhouse", "LODnaquadhouse"}}, 1 },
	{ "lc_indwindows12", {{"Chinabuilds09", "LODnabuilds09"}}, 1 },
	{ "lc_indwindows13", {{"Chinabuilds06", "LODnabuilds06"}}, 1 },
	{ "lc_indwindows14", {{"Chinabuilds05", "LODnabuilds05"}}, 1 },
	{ "lc_indwindows15", {{"Chinabuilds07", "LODnabuilds07"}, {"Chinabuilds21", "LODnabuilds21"}}, 2 },
	{ "lc_indwindows16", {{"Chinabuilds08", "LODnabuilds08"}}, 1 },
	{ "lc_indwindows17", {{"indhibuild1", "LODhibuild1"}}, 1 },
	{ "lc_inwindows12",  {{"ind_mainten3", "LOD_mainten3"}}, 1 },
	{ "lc_inwindows13",  {{"ind_newbuilds03", "LOD_newbuilds03"}, {"ind_land068", "LOD_land068"}, {"ind_newtenement08", "LOD_newtenement08"}}, 3 },
	{ "lc_inwindows14",  {{"indhibuild5", "LODhibuild5"}, {"ind_newbuilds02", "LOD_newbuilds02"}}, 2 },
	{ "lc_inwindows15",  {{"iten_block01", "LODn_block01"}, {"iten_hoteltop", "LODn_hoteltop"}}, 2 },
	{ "lc_inwindows16",  {{"indhibuild4", "LODhibuild4"}}, 1 },
	{ "lc_indwindows18", {{"ind_mainten2b", "LOD_mainten2b"}}, 1 },
	{ "lc_indwindows19", {{"ind_land097gnd", "LOD_land097gnd"}}, 1 },
	{ "lc_indwindows20", {{"indhibuild3B", "LODhibuild3B"}, {"ind_maindrag2", "LOD_maindrag2"}, {"ind_maindrag1", "LOD_maindrag1"}}, 3 },
	{ "lc_indwindows21", {{"indbilbridge1", "LODbilbridge1"}}, 1 },
	{ "lc_inwindows17",  {{"ind_newtenement06", "LOD_newtenement06"}}, 1 },
};

static int16
FindAttachedModelId(const char *name)
{
	int id = -1;
	CModelInfo::GetModelInfo(name, &id);
	return id;
}

static bool
IsScheduledAttachedCarrier(CEntity *candidate, const CVector &lightPosition,
	int16 detailModel, int16 lodModel, float renderDistance = -1.0f)
{
	if(candidate == nil ||
	   (candidate->GetModelIndex() != detailModel && candidate->GetModelIndex() != lodModel))
		return false;
	if((candidate->GetPosition() - lightPosition).MagnitudeSqr() >= 220.0f*220.0f ||
	   candidate->m_rwObject == nil || !candidate->bIsVisible)
		return false;

	CSimpleModelInfo *mi = (CSimpleModelInfo*)CModelInfo::GetModelInfo(candidate->GetModelIndex());
	if(mi->m_alpha == 0)
		return false;
	if(candidate->bDistanceFade){
		if(renderDistance < 0.0f)
			renderDistance = (candidate->GetPosition() - TheCamera.GetPosition()).Magnitude();
		renderDistance = CRenderer::GetNew3DSWorldDistance(candidate, renderDistance) /
			CRenderer::GetNew3DSWorldLodScale(mi, candidate->GetModelIndex(), candidate);
		const float fade = Max(0.0f, Min((mi->GetLargestLodDistance() -
			(renderDistance - FADE_DISTANCE))/FADE_DISTANCE, 1.0f));
		if(mi->m_alpha * fade < 16.0f)
			return false;
	}
	return true;
}
}

bool
CRenderer::ShouldRenderAttachedWindowLights(CEntity *ent)
{
	static bool modelsResolved;
	static LCSAttachedWindowModels windows[ARRAY_SIZE(kLCSAttachedWindows)];
	if(!modelsResolved){
		for(uint32 i = 0; i < ARRAY_SIZE(kLCSAttachedWindows); i++){
			windows[i].light = FindAttachedModelId(kLCSAttachedWindows[i].light);
			windows[i].numCarriers = kLCSAttachedWindows[i].numCarriers;
			for(uint32 carrier = 0; carrier < windows[i].numCarriers; carrier++)
				for(uint32 tier = 0; tier < 2; tier++)
					windows[i].carriers[carrier][tier] = FindAttachedModelId(kLCSAttachedWindows[i].carriers[carrier][tier]);
		}
		modelsResolved = true;
	}

	LCSAttachedWindowModels *attachment = nil;
	for(uint32 i = 0; i < ARRAY_SIZE(windows); i++)
		if(ent->GetModelIndex() == windows[i].light){
			attachment = &windows[i];
			break;
		}
	if(attachment == nil)
		return true;

	const CVector lightPosition = ent->GetPosition();
	for(uint32 carrier = 0; carrier < attachment->numCarriers; carrier++){
		const int16 detail = attachment->carriers[carrier][0];
		const int16 lod = attachment->carriers[carrier][1];
		bool found = false;
		for(int32 i = 0; i < ms_nNoOfVisibleEntities && !found; i++)
			found = IsScheduledAttachedCarrier(ms_aVisibleEntityPtrs[i], lightPosition, detail, lod);
#ifdef NEW_RENDERER
		for(int32 i = 0; i < ms_nNoOfVisibleBuildings && !found; i++)
			found = IsScheduledAttachedCarrier(ms_aVisibleBuildingPtrs[i], lightPosition, detail, lod);
		for(CLink<CVisibilityPlugins::AlphaObjectInfo> *node = CVisibilityPlugins::m_alphaBuildingList.head.next;
		    node != &CVisibilityPlugins::m_alphaBuildingList.tail && !found; node = node->next)
			found = IsScheduledAttachedCarrier(node->item.entity, lightPosition, detail, lod, node->item.sort);
#endif
		for(CLink<CVisibilityPlugins::AlphaObjectInfo> *node = CVisibilityPlugins::m_alphaEntityList.head.next;
		    node != &CVisibilityPlugins::m_alphaEntityList.tail && !found; node = node->next)
			found = IsScheduledAttachedCarrier(node->item.entity, lightPosition, detail, lod, node->item.sort);
		if(!found)
			return false;
	}
	return true;
}

#endif

void
CRenderer::Init(void)
{
	gSortedVehiclesAndPeds.Init(40);
	if(gMakeResources)
		SortBIGBuildings();
#ifdef _3DS
	InitLCSBuildingLodPairs();
#endif
}

void
CRenderer::Shutdown(void)
{
	gSortedVehiclesAndPeds.Shutdown();
}

void
CRenderer::PreRender(void)
{
#ifdef _3DS
	// Registration happens in vehicle PreRender, not in skipped logic frames.
	if(!CTimer::GetIsPaused())
		CSkidmarks::Update();
#endif
	int i;
	CLink<CVisibilityPlugins::AlphaObjectInfo> *node;

	for(i = 0; i < ms_nNoOfVisibleEntities; i++)
		ms_aVisibleEntityPtrs[i]->PreRender();

	for(i = 0; i < ms_nNoOfVisibleVehicles; i++)
		ms_aVisibleVehiclePtrs[i]->PreRender();
#ifdef NEW_RENDERER
	if(gbNewRenderer){
		// How is this done with cWorldStream?
		for(i = 0; i < ms_nNoOfVisibleBuildings; i++)
			ms_aVisibleBuildingPtrs[i]->PreRender();
		for(node = CVisibilityPlugins::m_alphaBuildingList.head.next;
		    node != &CVisibilityPlugins::m_alphaBuildingList.tail;
		    node = node->next)
			((CEntity*)node->item.entity)->PreRender();
	}
#endif

	for (i = 0; i < ms_nNoOfInVisibleEntities; i++) {
#ifdef SQUEEZE_PERFORMANCE
		if (ms_aInVisibleEntityPtrs[i]->IsVehicle() && ((CVehicle*)ms_aInVisibleEntityPtrs[i])->IsHeli())
#endif
		ms_aInVisibleEntityPtrs[i]->PreRender();
	}

	for(node = CVisibilityPlugins::m_alphaEntityList.head.next;
	    node != &CVisibilityPlugins::m_alphaEntityList.tail;
	    node = node->next)
		((CEntity*)node->item.entity)->PreRender();

	CHeli::SpecialHeliPreRender();
	CShadows::RenderExtraPlayerShadows();
}

void
CRenderer::RenderOneRoad(CEntity *e)
{
#ifndef MASTER
	if(gbDontRenderBuildings)
		return;
	if(gbShowCollisionPolys || gbShowCollisionPolysReflections || gbShowCollisionPolysNoShadows)
		CCollision::DrawColModel_Coloured(e->GetMatrix(), *CModelInfo::GetColModel(e->GetModelIndex()), e->GetModelIndex());
	else
#endif
	{
		PUSH_RENDERGROUP(CModelInfo::GetModelInfo(e->GetModelIndex())->GetModelName());

		e->Render();

		POP_RENDERGROUP();
	}
}

void
CRenderer::RenderOneNonRoad(CEntity *e)
{
	CPed *ped;
	CVehicle *veh;
	int i;
	bool resetLights;

#ifdef _3DS
	if(!ShouldRenderAttachedWindowLights(e))
		return;
#endif

#ifndef MASTER
	if(gbShowCollisionPolys || gbShowCollisionPolysReflections || gbShowCollisionPolysNoShadows){
		if(!e->IsVehicle()){
			CCollision::DrawColModel_Coloured(e->GetMatrix(), *CModelInfo::GetColModel(e->GetModelIndex()), e->GetModelIndex());
			return;
		}
	}else
#endif
#ifndef FINAL
	if(e->IsBuilding()){
		if(e->bIsBIGBuilding){
			if(gbDontRenderBigBuildings)
				return;
		}else{
			if(gbDontRenderBuildings)
				return;
		}
	}else
#endif
	if(e->IsPed()){
#ifndef FINAL
		if(gbDontRenderPeds)
			return;
#endif
		ped = (CPed*)e;
		if(ped->m_nPedState == PED_DRIVING)
			return;
	}
#ifndef FINAL
	else if(e->IsObject() || e->IsDummy()){
		if(gbDontRenderObjects)
			return;
	}else if(e->IsVehicle()){
		// re3 addition
		if(gbDontRenderVehicles)
			return;
	}
#endif

	PUSH_RENDERGROUP(CModelInfo::GetModelInfo(e->GetModelIndex())->GetModelName());

#ifdef _3DS
	CrowdDrawBudget3DS::RenderScope crowdStyle(e);
	CameraOcclusion3DS::RenderScope cameraFade(e);
#endif
	resetLights = e->SetupLighting();

	if(e->IsVehicle()){
		CVisibilityPlugins::InitAlphaAtomicList();
		if(e->m_rwObject && RwObjectGetType(e->m_rwObject) == rpCLUMP)
			CVisibilityPlugins::SetupVehicleVariables((RpClump*)e->m_rwObject);
	}

	// Render Peds in vehicle before vehicle itself
	if(e->IsVehicle()){
		veh = (CVehicle*)e;
		bool renderOccupants = true;
#ifdef _3DS
		RpClump *vehicleClump = e->m_rwObject && RwObjectGetType(e->m_rwObject) == rpCLUMP ?
			(RpClump*)e->m_rwObject : nil;
		const bool bigVehicle = veh->IsTrain() || veh->IsHeli() || veh->IsPlane();
		renderOccupants = vehicleClump &&
			CVisibilityPlugins::IsVehicleHighDetail(vehicleClump, bigVehicle);
#endif
#ifdef VIS_DISTANCE_ALPHA
		int vehalpha = CVisibilityPlugins::GetObjectDistanceAlpha(veh->m_rwObject);
#endif
		if(renderOccupants){
		if(veh->pDriver && veh->pDriver->m_nPedState == PED_DRIVING){
#ifdef VIS_DISTANCE_ALPHA
			int alpha = CVisibilityPlugins::GetObjectDistanceAlpha(veh->pDriver->m_rwObject);
			CVisibilityPlugins::SetObjectDistanceAlpha(veh->pDriver->m_rwObject, vehalpha);
			veh->pDriver->Render();
			CVisibilityPlugins::SetObjectDistanceAlpha(veh->pDriver->m_rwObject, alpha);
#else
			veh->pDriver->Render();
#endif
		}
		for(i = 0; i < 8; i++)
			if(veh->pPassengers[i] && veh->pPassengers[i]->m_nPedState == PED_DRIVING){
#ifdef VIS_DISTANCE_ALPHA
				int alpha = CVisibilityPlugins::GetObjectDistanceAlpha(veh->pPassengers[i]->m_rwObject);
				CVisibilityPlugins::SetObjectDistanceAlpha(veh->pPassengers[i]->m_rwObject, vehalpha);
				veh->pPassengers[i]->Render();
				CVisibilityPlugins::SetObjectDistanceAlpha(veh->pPassengers[i]->m_rwObject, alpha);
#else
				veh->pPassengers[i]->Render();
#endif
		}
		}
	SetCullMode(rwCULLMODECULLNONE);
	}

#ifdef _3DS
	/*
	 * LCS foliage has no lower-detail mesh: a single visible tree-patch can
	 * contain a large number of alpha leaf cards.  The generic PS2 GS alpha
	 * emulation draws every alpha mesh twice (opaque core, then soft edge),
	 * which is useful for vehicles and glass but turns dense vegetation into a
	 * fill-rate and submission bottleneck.  Keep its full 1.2 visibility range
	 * and normal alpha cut-out, but render foliage in one pass.  The original
	 * state is restored immediately so non-foliage still uses the PS2 path.
	 */
	const bool singlePassFoliage = !e->IsVehicle() && IsLCSVegetationModel(e->GetModelIndex());
	uint32 savedGSAlphaTest = 0;
	uint32 savedAlphaTestFunc = 0;
	uint32 savedAlphaTestRef = 0;
	if(singlePassFoliage){
		savedGSAlphaTest = rw::GetRenderState(rw::GSALPHATEST);
		if(savedGSAlphaTest){
			savedAlphaTestFunc = rw::GetRenderState(rw::ALPHATESTFUNC);
			savedAlphaTestRef = rw::GetRenderState(rw::ALPHATESTREF);
			/* A plain one-pass draw would inherit the world's very low alpha
			 * threshold (3).  Filtered transparent texels around leaf cards then
			 * pass and write depth, punching rectangular holes through everything
			 * behind the tree.  Keep only the opaque-core half of the PS2 path by
			 * using its 128 cutoff, but skip the costly soft-edge second draw. */
			rw::SetRenderState(rw::GSALPHATEST, false);
			rw::SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAGREATEREQUAL);
			rw::SetRenderState(rw::ALPHATESTREF,
				rw::GetRenderState(rw::GSALPHATESTREF));
		}
	}
	e->Render();
	if(singlePassFoliage && savedGSAlphaTest){
		rw::SetRenderState(rw::ALPHATESTFUNC, savedAlphaTestFunc);
		rw::SetRenderState(rw::ALPHATESTREF, savedAlphaTestRef);
		rw::SetRenderState(rw::GSALPHATEST, savedGSAlphaTest);
	}
#else
	e->Render();
#endif

	if(e->IsVehicle()){
		// TODO(LCS): LCS does not use bImBeingRendered, keeping it for safety
		e->bImBeingRendered = true;
		CVisibilityPlugins::RenderAlphaAtomics();
		e->bImBeingRendered = false;
		SetCullMode(rwCULLMODECULLBACK);
	}

	e->RemoveLighting(resetLights);

	POP_RENDERGROUP();
}

void
CRenderer::RenderFirstPersonVehicle(void)
{
	if(m_pFirstPersonVehicle == nil)
		return;
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RenderOneNonRoad(m_pFirstPersonVehicle);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
}

inline bool IsRoad(CEntity *e) { return e->IsBuilding() && ((CSimpleModelInfo*)CModelInfo::GetModelInfo(e->GetModelIndex()))->m_wetRoadReflection; }


void
CRenderer::RenderRoads(void)
{
	int i;
	CEntity *e;

	PUSH_RENDERGROUP("CRenderer::RenderRoads");
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
//	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	SetCullMode(rwCULLMODECULLBACK);
	DeActivateDirectional();
	SetAmbientColours();


	for(i = 0; i < ms_nNoOfVisibleEntities; i++){
		e = ms_aVisibleEntityPtrs[i];
		if(IsRoad(e))
			RenderOneRoad(e);
	}
	POP_RENDERGROUP();
}

inline bool PutIntoSortedVehicleList(CVehicle *veh)
{
	if(veh->IsBoat()){
		int mode = TheCamera.Cams[TheCamera.ActiveCam].Mode;
		if(mode == CCam::MODE_WHEELCAM ||
		   mode == CCam::MODE_1STPERSON && TheCamera.GetLookDirection() != LOOKING_FORWARD && TheCamera.GetLookDirection() != LOOKING_BEHIND ||
		   CVisibilityPlugins::GetClumpAlpha(veh->GetClump()) != 255)
			return false;
		return true;
	}else
		return veh->bTouchingWater;		
}

// this only renders objects in LCS
void
CRenderer::RenderEverythingBarRoads(void)
{
	int i;
	CEntity *e;

	PUSH_RENDERGROUP("CRenderer::RenderEverythingBarRoads");
//	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
//	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	SetCullMode(rwCULLMODECULLBACK);
//	gSortedVehiclesAndPeds.Clear();

	for(i = 0; i < ms_nNoOfVisibleEntities; i++){
		e = ms_aVisibleEntityPtrs[i];

		if(IsRoad(e))
			continue;

#ifdef EXTENDED_PIPELINES
		if(CustomPipes::bRenderingEnvMap && (e->IsPed() || e->IsVehicle()))
			continue;
#endif

		// we're not even rendering peds here....
#ifdef VIS_DISTANCE_ALPHA
		// this looks like a fix for objects just popping in
		int distAlpha = CVisibilityPlugins::GetObjectDistanceAlpha(e->m_rwObject);
		if(e->IsPed() && CVisibilityPlugins::GetClumpAlpha((RpClump*)e->m_rwObject) != 255 ||
		   distAlpha != 255){
			if(distAlpha != 0 && !CVisibilityPlugins::InsertEntityIntoSortedList(e, (ms_vecCameraPosition - e->GetPosition()).Magnitude())){
#else
		if(e->IsPed() && CVisibilityPlugins::GetClumpAlpha((RpClump*)e->m_rwObject) != 255){
			if(!CVisibilityPlugins::InsertEntityIntoSortedList(e, (ms_vecCameraPosition - e->GetPosition()).Magnitude())){
#endif
				printf("Ran out of space in alpha entity list");
				RenderOneNonRoad(e);
			}
		}else
			RenderOneNonRoad(e);
	}
	POP_RENDERGROUP();
}

void
CRenderer::RenderBoats(void)
{
	int i;
	CEntity *e;
	EntityInfo ei;
	CLink<EntityInfo> *node;

	PUSH_RENDERGROUP("CRenderer::RenderBoats");
	gbLighting = true;
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
//	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	SetCullMode(rwCULLMODECULLBACK);

	gSortedVehiclesAndPeds.Clear();
	for(i = 0; i < ms_nNoOfVisibleVehicles; i++){
		e = ms_aVisibleVehiclePtrs[i];
		if(e->IsVehicle() && PutIntoSortedVehicleList((CVehicle*)e)){
			ei.ent = e;
			ei.sort = (ms_vecCameraPosition - e->GetPosition()).MagnitudeSqr();
			gSortedVehiclesAndPeds.InsertSorted(ei);
		}
	}

	for(node = gSortedVehiclesAndPeds.tail.prev;
	    node != &gSortedVehiclesAndPeds.head;
	    node = node->prev){
		CVehicle *v = (CVehicle*)node->item.ent;
		RenderOneNonRoad(v);
	}
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	gbLighting = false;
	POP_RENDERGROUP();
}

// also renders peds
void
CRenderer::RenderVehicles(void)
{
	int i;
	CEntity *e;

#if defined(RW_3DS) && defined(RESTORIES_3DS_BUILD)
	/* MatFX may defer individual atomics into an alpha list, so publish the
	 * player's clump for the whole vehicle pass instead of toggling a transient
	 * flag around CEntity::Render. */
	CVehicle *playerVehicle = FindPlayerVehicle();
	rw::c3d::setPlayerVehicleClump(playerVehicle ?
		(RpClump*)playerVehicle->m_rwObject : nil);
#endif

	PUSH_RENDERGROUP("CRenderer::RenderVehicles");
	// LCS: not on android
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	// TODO(LCS): PS2VehicleAlphaFunc();

	gbLighting = true;
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);

	CVisibilityPlugins::InitAlphaEntityList();
	for(i = 0; i < ms_nNoOfVisibleVehicles; i++){
		e = ms_aVisibleVehiclePtrs[i];
#ifdef VIS_DISTANCE_ALPHA
		if(CVisibilityPlugins::GetClumpAlpha((RpClump*)e->m_rwObject) == 0 ||
		   CVisibilityPlugins::GetObjectDistanceAlpha(e->m_rwObject) == 0)
			continue;
#endif

		int behindDriver = e->bIsPed && ((CPed*)e)->m_nPedState == PED_DRIVING &&
				TheCamera.GetLookDirection() == LOOKING_FORWARD;
		// what is going on here? !behindDriver will always be true because we're checking for !PED_DRIVING
		if(!e->bDistanceFade && (e->IsPed() || e->bIsPed) && ((CPed*)e)->m_nPedState != PED_DRIVING && !behindDriver){
#ifdef VIS_DISTANCE_ALPHA
			if(CVisibilityPlugins::GetClumpAlpha((RpClump*)e->m_rwObject) != 255 ||
			   CVisibilityPlugins::GetObjectDistanceAlpha(e->m_rwObject) != 255)
				;	// set blend render states
			else
				;	// set default render states
#endif
			RenderOneNonRoad(e);
		}else if(!PutIntoSortedVehicleList((CVehicle*)e) &&	// boats handled elsewhere
		   !CVisibilityPlugins::InsertEntityIntoSortedList(e, (ms_vecCameraPosition - e->GetPosition()).Magnitude())){
#ifdef VIS_DISTANCE_ALPHA
			if(CVisibilityPlugins::GetClumpAlpha((RpClump*)e->m_rwObject) != 255 ||
			   CVisibilityPlugins::GetObjectDistanceAlpha(e->m_rwObject) != 255)
				;	// set blend render states
			else
				;	// set default render states
#endif
			printf("Ran out of space in alpha entity list");
			RenderOneNonRoad(e);
		}
	}
	CVisibilityPlugins::RenderFadingEntities();

	CFerry::RenderAllRemaning();
	CPlane::RenderAllRemaning();
	// TODO(LCS): gpGlobalEnvironmentMap = nil;
	// TODO(LCS): CMattRenderer::Get().ResetRenderStates();
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	gbLighting = false;
	POP_RENDERGROUP();
}

#ifdef NEW_RENDERER
#ifndef LIBRW
#error "Need librw for EXTENDED_PIPELINES"
#endif

enum {
	// blend passes
	PASS_NOZ,	// no z-write
	PASS_ADD,	// additive
	PASS_BLEND	// normal blend
};

static void
SetStencilState(int state)
{
	switch(state){
	// disable stencil
	case 0:
		rw::SetRenderState(rw::STENCILENABLE, FALSE);
		break;
	// test against stencil
	case 1:
		rw::SetRenderState(rw::STENCILENABLE, TRUE);
		rw::SetRenderState(rw::STENCILFUNCTION, rw::STENCILNOTEQUAL);
		rw::SetRenderState(rw::STENCILPASS, rw::STENCILKEEP);
		rw::SetRenderState(rw::STENCILFAIL, rw::STENCILKEEP);
		rw::SetRenderState(rw::STENCILZFAIL, rw::STENCILKEEP);
		rw::SetRenderState(rw::STENCILFUNCTIONMASK, 0xFF);
		rw::SetRenderState(rw::STENCILFUNCTIONREF, 0xFF);
		break;
	// write to stencil
	case 2:
		rw::SetRenderState(rw::STENCILENABLE, TRUE);
		rw::SetRenderState(rw::STENCILFUNCTION, rw::STENCILALWAYS);
		rw::SetRenderState(rw::STENCILPASS, rw::STENCILREPLACE);
		rw::SetRenderState(rw::STENCILFUNCTIONREF, 0xFF);
		break;
	}
}

void
CRenderer::RenderOneBuilding(CEntity *ent, float camdist)
{
	if(ent->m_rwObject == nil)
		return;
#ifdef _3DS
	if(!ShouldRenderAttachedWindowLights(ent))
		return;
#endif

	ent->bImBeingRendered = true;	// TODO: this seems wrong, but do we even need it?

	assert(RwObjectGetType(ent->m_rwObject) == rpATOMIC);
	RpAtomic *atomic = (RpAtomic*)ent->m_rwObject;
	CSimpleModelInfo *mi = (CSimpleModelInfo*)CModelInfo::GetModelInfo(ent->GetModelIndex());

	int pass = PASS_BLEND;
	if(mi->m_additive)	// very questionable
		pass = PASS_ADD;
	if(mi->m_noZwrite)
		pass = PASS_NOZ;

	if(ent->bDistanceFade){
		RpAtomic *lodatm;
		float fadefactor;
		uint32 alpha;
		float lodCamDist = camdist;
#ifdef _3DS
		/* Visibility selected this atomic with the scaled New3DS LOD distance.
		 * Use the identical distance for the fade/geometry handoff, otherwise
		 * related HD and LOD buildings overlap and z-fight in dense districts. */
		lodCamDist /= GetNew3DSWorldLodScale(mi, ent->GetModelIndex(), ent);
#endif

		lodatm = mi->GetAtomicFromDistance(lodCamDist - FADE_DISTANCE);
		/* LCS models do not all provide an atomic for every distance reached
		 * with the reduced New3DS LOD range.  Keep fading the resident atomic
		 * instead of dereferencing a missing LOD below. */
		if(lodatm == nil)
			lodatm = atomic;
		fadefactor = (mi->GetLargestLodDistance() - (lodCamDist - FADE_DISTANCE))/FADE_DISTANCE;
		if(fadefactor > 1.0f)
			fadefactor = 1.0f;
		alpha = mi->m_alpha * Max(0.0f, Min(fadefactor, 1.0f));
		if(alpha == 0){
			ent->bImBeingRendered = false;
			return;
		}

		if(alpha == 255)
			WorldRender::AtomicFirstPass(atomic, pass);
		else{
			// not quite sure what this is about, do we have to do that?
			RpGeometry *geo = RpAtomicGetGeometry(lodatm);
			if(geo != RpAtomicGetGeometry(atomic))
				RpAtomicSetGeometry(atomic, geo, rpATOMICSAMEBOUNDINGSPHERE);
			WorldRender::AtomicFullyTransparent(atomic, pass, alpha);
		}
	}else
		WorldRender::AtomicFirstPass(atomic, pass);

	ent->bImBeingRendered = false;	// TODO: this seems wrong, but do we even need it?
}

// our replacement for cWorldStream::Render
void
CRenderer::RenderWorld(int pass)
{
	int i;
	CEntity *e;
	CLink<CVisibilityPlugins::AlphaObjectInfo> *node;
	// use old renderer
	if(gbPreviewCity)
		return;

	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	SetCullMode(rwCULLMODECULLBACK);
	DeActivateDirectional();
	SetAmbientColours();

	// Temporary...have to figure out sorting better
	switch(pass){
	case 0:
		// Roads
		PUSH_RENDERGROUP("CRenderer::RenderWorld - Roads");
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
		for(i = 0; i < ms_nNoOfVisibleBuildings; i++){
			e = ms_aVisibleBuildingPtrs[i];
			if(e->bIsBIGBuilding || IsRoad(e))
				RenderOneBuilding(e);
		}
		for(node = CVisibilityPlugins::m_alphaBuildingList.tail.prev;
		    node != &CVisibilityPlugins::m_alphaBuildingList.head;
		    node = node->prev){
			e = node->item.entity;
			if(e->bIsBIGBuilding || IsRoad(e))
				RenderOneBuilding(e, node->item.sort);
		}
		POP_RENDERGROUP();
		break;
	case 1:
		// Opaque
		PUSH_RENDERGROUP("CRenderer::RenderWorld - Opaque");
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
		for(i = 0; i < ms_nNoOfVisibleBuildings; i++){
			e = ms_aVisibleBuildingPtrs[i];
			if(!(e->bIsBIGBuilding || IsRoad(e)))
				RenderOneBuilding(e);
		}
		for(node = CVisibilityPlugins::m_alphaBuildingList.tail.prev;
		    node != &CVisibilityPlugins::m_alphaBuildingList.head;
		    node = node->prev){
			e = node->item.entity;
			if(!(e->bIsBIGBuilding || IsRoad(e)))
				RenderOneBuilding(e, node->item.sort);
		}
		// Now we have iterated through all visible buildings (unsorted and sorted)
		// and the transparency list is done.

		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, FALSE);
		WorldRender::RenderBlendPass(PASS_NOZ);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
		POP_RENDERGROUP();
		break;
	case 2:
		// Transparent
		PUSH_RENDERGROUP("CRenderer::RenderWorld - Transparent");
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
		WorldRender::RenderBlendPass(PASS_ADD);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
		WorldRender::RenderBlendPass(PASS_BLEND);
		POP_RENDERGROUP();
		break;
	}
}
#endif

#ifndef NEW_RENDERER
static void
SetStencilState(int)
{
}
#endif

// actually CMattRenderer::RenderWater
// adapted slightly
void
CRenderer::RenderWater(void)
{
	int i;
	CEntity *e;

	PUSH_RENDERGROUP("CRenderer::RenderWater");

	gbLighting = false;
	CWaterLevel::RenderWater();

	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nil);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDZERO);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	SetStencilState(2);

	for(i = 0; i < ms_nNoOfVisibleVehicles; i++){
		e = ms_aVisibleVehiclePtrs[i];
		if(e->IsVehicle() && ((CVehicle*)e)->IsBoat())
			((CBoat*)e)->RenderWaterOutPolys();
	}

	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	SetStencilState(1);

	CWaterLevel::RenderTransparentWater();

	SetStencilState(0);
	gbLighting = true;
	POP_RENDERGROUP();
}

void
CRenderer::ClearForFrame(void)
{
	ms_nNoOfVisibleEntities = 0;
	ms_nNoOfVisibleVehicles = 0;
	ms_nNoOfInVisibleEntities = 0;
	gSortedVehiclesAndPeds.Clear();
#ifdef _3DS
	gLCSVisibleBuildingLodSources.clear();
#endif

#ifdef NEW_RENDERER
	ms_nNoOfVisibleBuildings = 0;
	WorldRender::numBlendInsts[PASS_NOZ] = 0;
	WorldRender::numBlendInsts[PASS_ADD] = 0;
	WorldRender::numBlendInsts[PASS_BLEND] = 0;
#endif
}

void
CRenderer::RenderFadingInEntities(void)
{
	PUSH_RENDERGROUP("CRenderer::RenderFadingInEntities");
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
//	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	SetCullMode(rwCULLMODECULLBACK);
	DeActivateDirectional();
	SetAmbientColours();
	CVisibilityPlugins::RenderFadingEntities();
	POP_RENDERGROUP();
}

void
CRenderer::RenderFadingInUnderwaterEntities(void)
{
	PUSH_RENDERGROUP("CRenderer::RenderFadingInUnderwaterEntities");
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
//	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	SetCullMode(rwCULLMODECULLBACK);
	DeActivateDirectional();
	SetAmbientColours();
	CVisibilityPlugins::RenderFadingUnderwaterEntities();
	POP_RENDERGROUP();
}

void
CRenderer::RenderCollisionLines(void)
{
	int i;

	// game doesn't draw fading in entities
	// this should probably be fixed
	for(i = 0; i < ms_nNoOfVisibleEntities; i++){
		CEntity *e = ms_aVisibleEntityPtrs[i];
		if(Abs(e->GetPosition().x - ms_vecCameraPosition.x) < 100.0f &&
		   Abs(e->GetPosition().y - ms_vecCameraPosition.y) < 100.0f)
			CCollision::DrawColModel(e->GetMatrix(), *e->GetColModel());
	}
}

enum Visbility
{
	VIS_INVISIBLE,
	VIS_VISIBLE,
	VIS_OFFSCREEN,
	VIS_STREAMME
};

// Time Objects can be time culled if
//   other == -1 || CModelInfo::GetModelInfo(other)->GetRwObject()
// i.e. we have to draw even at the wrong time if
//   other != -1 && CModelInfo::GetModelInfo(other)->GetRwObject() == nil

#define OTHERUNAVAILABLE (other != -1 && CModelInfo::GetModelInfo(other)->GetRwObject() == nil)
#define CANTIMECULL (!OTHERUNAVAILABLE)

int32
CRenderer::SetupEntityVisibility(CEntity *ent)
{
#ifdef _3DS
	if((ent->IsVehicle() || ent->IsPed()) && CrowdDrawBudget3DS::Hide(ent))
		return VIS_OFFSCREEN;
#endif
	CSimpleModelInfo *mi = (CSimpleModelInfo*)CModelInfo::GetModelInfo(ent->m_modelIndex);
	CTimeModelInfo *ti;
	int32 other;
	float dist;

	bool request = true;
	if(mi->GetModelType() == MITYPE_SIMPLE){
		// TODO(LCS): some cWorldStream dynamics stuff
	}
	if(mi->GetModelType() == MITYPE_TIME){
 		ti = (CTimeModelInfo*)mi;
		other = ti->GetOtherTimeModel();
		if(CClock::GetIsTimeInRange(ti->GetTimeOn(), ti->GetTimeOff())){
			// don't fade in, or between time objects
			if(CANTIMECULL)
				ti->m_alpha = 255;
		}else{
			// Hide if possible
			if(CANTIMECULL){
				ent->DeleteRwObject();
				return VIS_INVISIBLE;
			}
			// can't cull, so we'll try to draw this one, but don't request
			// it since what we really want is the other one.
			request = false;
		}
	}else{
		if(mi->GetModelType() != MITYPE_SIMPLE && mi->GetModelType() != MITYPE_WEAPON){
			if(FindPlayerVehicle() == ent &&
#ifdef _3DS
			   !CameraOcclusion3DS::PresetVisible(ent) &&
#endif
			   TheCamera.Cams[TheCamera.ActiveCam].Mode == CCam::MODE_1STPERSON &&
// TODO(LCS): something in CWorld::Players...
			   !(FindPlayerVehicle()->IsBike() && ((CBike*)FindPlayerVehicle())->bWheelieCam)){
				// Player's vehicle in first person mode
				CVehicle *veh = (CVehicle*)ent;
				int model = veh->GetModelIndex();
				int direction = TheCamera.Cams[TheCamera.ActiveCam].DirectionWasLooking;
				if(direction == LOOKING_FORWARD ||
				   ent->GetModelIndex() == MI_RHINO ||
				   ent->GetModelIndex() == MI_BUS ||
				   ent->GetModelIndex() == MI_COACH ||
				   TheCamera.m_bInATunnelAndABigVehicle ||
				   direction == LOOKING_BEHIND && veh->pHandling->Flags & HANDLING_UNKNOWN){
					ent->bNoBrightHeadLights = true;
					return VIS_OFFSCREEN;
				}

				if(direction != LOOKING_BEHIND ||
				   !veh->IsBoat() || model == MI_REEFER || model == MI_PREDATOR){
					m_pFirstPersonVehicle = (CVehicle*)ent;
					ent->bNoBrightHeadLights = false;
					return VIS_OFFSCREEN;
				}
			}

			// All sorts of Clumps
			if(ent->m_rwObject == nil || (!ent->bIsVisible
#ifdef _3DS
			   && !CameraOcclusion3DS::PresetVisible(ent)
#endif
			   ))
				return VIS_INVISIBLE;
			if(!ent->GetIsOnScreen() || ent->IsEntityOccluded())
				return VIS_OFFSCREEN;
			if(ent->bDrawLast){
				dist = (ent->GetPosition() - ms_vecCameraPosition).Magnitude();
				CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist);
				ent->bDistanceFade = false;
				return VIS_INVISIBLE;
			}
			return VIS_VISIBLE;
		}
		if(ent->bDontStream){
			if(ent->m_rwObject == nil || (!ent->bIsVisible
#ifdef _3DS
			   && !CameraOcclusion3DS::PresetVisible(ent)
#endif
			   ))
				return VIS_INVISIBLE;
			if(!ent->GetIsOnScreen() || ent->IsEntityOccluded())
				return VIS_OFFSCREEN;
			if(ent->bDrawLast){
				dist = (ent->GetPosition() - ms_vecCameraPosition).Magnitude();
				CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist);
				ent->bDistanceFade = false;
				return VIS_INVISIBLE;
			}
			return VIS_VISIBLE;
		}
	}

	// Simple ModelInfo

	if(!IsAreaVisible(ent->m_area))
		return VIS_INVISIBLE;

	dist = (ent->GetPosition() - ms_vecCameraPosition).Magnitude();
#ifdef _3DS
	const LCSWorldBounds bounds(ent, ms_vecCameraPosition);
	const float lodDist = GetLCSWorldDistance(ent, dist, bounds) /
		GetLCSWorldLodScale(mi, ent->m_modelIndex, ent, bounds);
#else
	const float lodDist = dist;
#endif

//#ifndef FIX_BUGS
#if 0
	// Whatever this is supposed to do, it breaks fading for objects
	// whose draw dist is > LOD_DISTANCE-FADE_DISTANCE, i.e. 280
	// because decreasing dist here makes the object visible above LOD_DISTANCE
	// before fading normally once below LOD_DISTANCE.
	// aha! this must be a workaround for the fact that we're not taking
	// the LOD multiplier into account here anywhere
	if(LOD_DISTANCE < dist && dist < mi->GetLargestLodDistance() + FADE_DISTANCE)
		dist += mi->GetLargestLodDistance() - LOD_DISTANCE;
#endif
	// LCS has this now, wonder if it's any better:
	float atomicDist = lodDist;
	if(LOD_DISTANCE + STREAM_DISTANCE < atomicDist && atomicDist < mi->GetLargestLodDistance())
		atomicDist = mi->GetLargestLodDistance();

	if(ent->IsObject() && ent->bRenderDamaged)
		mi->m_isDamaged = true;

	RpAtomic *a = mi->GetAtomicFromDistance(atomicDist);
	if(a){
		mi->m_isDamaged = false;
		if(ent->m_rwObject == nil)
			ent->CreateRwObject();
		assert(ent->m_rwObject);
		RpAtomic *rwobj = (RpAtomic*)ent->m_rwObject;
		// Make sure our atomic uses the right geometry and not
		// that of an atomic for another draw distance.
		if(RpAtomicGetGeometry(a) != RpAtomicGetGeometry(rwobj))
			RpAtomicSetGeometry(rwobj, RpAtomicGetGeometry(a), rpATOMICSAMEBOUNDINGSPHERE); // originally 5 (mistake?)
		mi->IncreaseAlpha();
		if(ent->m_rwObject == nil || (!ent->bIsVisible
#ifdef _3DS
			   && !CameraOcclusion3DS::PresetVisible(ent)
#endif
			   ))
			return VIS_INVISIBLE;

		if(!ent->GetIsOnScreen() || ent->IsEntityOccluded()){
			mi->m_alpha = 255;
			return VIS_OFFSCREEN;
		}
		const bool pairedHigh3DS=HasLCSBuildingLodPair(ent->m_modelIndex,LCS_HD_HAS_LOD);
		if(mi->m_alpha != 255 || (!pairedHigh3DS &&
			   lodDist > mi->GetLargestLodDistance() - FADE_DISTANCE)){
			CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist);
			ent->bDistanceFade = true;
			return VIS_INVISIBLE;
		}
#ifdef _3DS
		// Register the detailed instance only after its fade has actually
		// completed. Model alpha is shared by every instance, so registering
		// while this entity is still in the alpha list can remove its coarse
		// partner a frame before any opaque replacement is drawn.
		MarkLCSVisibleBuildingLodSource(ent);
#endif

		if(mi->m_drawLast || ent->bDrawLast){
			if(CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist)){
				ent->bDistanceFade = false;
				return VIS_INVISIBLE;
			}
		}
		ent->bDistanceFade = false;
		return VIS_VISIBLE;
	}

	// Object is not loaded, figure out what to do

	if(mi->m_noFade){
		mi->m_isDamaged = false;
		// request model
		if(lodDist - STREAM_DISTANCE < mi->GetLargestLodDistance() && request)
			return VIS_STREAMME;
		return VIS_INVISIBLE;
	}

	// We might be fading

	a = mi->GetLastAtomic(lodDist - FADE_DISTANCE);
	mi->m_isDamaged = false;
	if(a == nil){
		// request model
		if(lodDist - FADE_DISTANCE - STREAM_DISTANCE < mi->GetLargestLodDistance() && request)
			return VIS_STREAMME;
		return VIS_INVISIBLE;
	}

	if(ent->m_rwObject == nil)
		ent->CreateRwObject();
	assert(ent->m_rwObject);
	RpAtomic *rwobj = (RpAtomic*)ent->m_rwObject;
	if(RpAtomicGetGeometry(a) != RpAtomicGetGeometry(rwobj))
		RpAtomicSetGeometry(rwobj, RpAtomicGetGeometry(a), rpATOMICSAMEBOUNDINGSPHERE); // originally 5 (mistake?)
	mi->IncreaseAlpha();
	if(ent->m_rwObject == nil || (!ent->bIsVisible
#ifdef _3DS
			   && !CameraOcclusion3DS::PresetVisible(ent)
#endif
			   ))
		return VIS_INVISIBLE;

	if(!ent->GetIsOnScreen() || ent->IsEntityOccluded()){
		mi->m_alpha = 255;
		return VIS_OFFSCREEN;
	}else{
		CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist);
		ent->bDistanceFade = true;
		return VIS_OFFSCREEN;	// Why this?
	}
}

int32
CRenderer::SetupBigBuildingVisibility(CEntity *ent)
{
	CSimpleModelInfo *mi = (CSimpleModelInfo*)CModelInfo::GetModelInfo(ent->m_modelIndex);
	CTimeModelInfo *ti;
	int32 other;

	if(!IsAreaVisible(ent->m_area))
		return VIS_INVISIBLE;

	bool request = true;
	if(mi->GetModelType() == MITYPE_TIME){
		ti = (CTimeModelInfo*)mi;
		other = ti->GetOtherTimeModel();
		if(CClock::GetIsTimeInRange(ti->GetTimeOn(), ti->GetTimeOff())){
			// don't fade in, or between time objects
			if(CANTIMECULL)
				ti->m_alpha = 255;
		}else{
			// Hide if possible
			if(CANTIMECULL){
				ent->DeleteRwObject();
				return VIS_INVISIBLE;
			}
			// can't cull, so we'll try to draw this one, but don't request
			// it since what we really want is the other one.
			request = false;
		}
	}else if(mi->GetModelType() == MITYPE_VEHICLE)
		return ent->IsVisible() ? VIS_VISIBLE : VIS_INVISIBLE;

	float dist = (ms_vecCameraPosition-ent->GetPosition()).Magnitude();
#ifdef _3DS
	const LCSWorldBounds bounds(ent, ms_vecCameraPosition);
	const float lodDist = GetLCSWorldDistance(ent, dist, bounds) /
		GetLCSWorldLodScale(mi, ent->m_modelIndex, ent, bounds);
#else
	const float lodDist = dist;
#endif
	CSimpleModelInfo *nonLOD = mi->GetRelatedModel();
	float nearLodDist = lodDist;
#ifdef _3DS
	if(ent->IsBuilding() && HasLCSBuildingLodPair(ent->m_modelIndex, LCS_LOD_HAS_HD)){
		// Use the paired HD building's actual profile and adaptive contraction
		// for the coarse model's near handoff. Otherwise the HD model can end
		// before its LOD is allowed to begin, leaving a completely empty block.
		const bool stereo = rw::c3d::stereoControlsActive();
		const bool performance = rw::c3d::performanceModeActive();
		const float hdScale = stereo ? (performance ? 0.40f : 0.57f) :
			(performance ? 0.45f : 0.65f);
		const float surfaceDistance = Max(0.f, bounds.distance-bounds.radius);
		nearLodDist = GetLCSWorldDistance(ent,dist,bounds) /
			(hdScale * CrowdDrawBudget3DS::DetailDistanceFactor(surfaceDistance));
	}
#endif

	// Find out whether to draw below near distance.
	// Advance only the paired coarse model's near handoff, not its far range.
#ifdef _3DS
	if(ent->IsBuilding() && HasLCSBuildingLodPair(ent->m_modelIndex, LCS_LOD_HAS_HD))
		nearLodDist /= LCS_BUILDING_HD_RANGE;
#endif
	// This is only the case if there is a non-LOD which is either not
	// loaded or not completely faded in yet.
	if(nearLodDist < mi->GetNearDistance() && nearLodDist < LOD_DISTANCE + STREAM_DISTANCE){
		// No non-LOD or non-LOD is completely visible.
		const bool paired3DS = HasLCSBuildingLodPair(ent->m_modelIndex, LCS_LOD_HAS_HD);
		const bool sourceVisible3DS = paired3DS ? HasVisibleLCSBuildingLodSource(ent, nonLOD) : true;
		if(nonLOD == nil || sourceVisible3DS && nonLOD->GetRwObject() && nonLOD->m_alpha == 255)
			return VIS_INVISIBLE;

		// But if it is a time object, we'd rather draw the wrong
		// non-LOD than the right LOD.
		if(nonLOD->GetModelType() == MITYPE_TIME){
			ti = (CTimeModelInfo*)nonLOD;
			other = ti->GetOtherTimeModel();
			if(other != -1 && CModelInfo::GetModelInfo(other)->GetRwObject())
				return VIS_INVISIBLE;
		}
	}

	RpAtomic *a = mi->GetFirstAtomicFromDistance(lodDist);
	if(a){
		if(ent->m_rwObject == nil)
			ent->CreateRwObject();
		assert(ent->m_rwObject);
		RpAtomic *rwobj = (RpAtomic*)ent->m_rwObject;

		// Make sure our atomic uses the right geometry and not
		// that of an atomic for another draw distance.
		if(RpAtomicGetGeometry(a) != RpAtomicGetGeometry(rwobj))
			RpAtomicSetGeometry(rwobj, RpAtomicGetGeometry(a), rpATOMICSAMEBOUNDINGSPHERE); // originally 5 (mistake?)
		mi->IncreaseAlpha();
		if(!ent->IsVisible() || !ent->GetIsOnScreenComplex() || ent->IsEntityOccluded()){
			mi->m_alpha = 255;
			return VIS_INVISIBLE;
		}

		if(mi->m_alpha != 255 ||
		   lodDist > mi->GetLargestLodDistance() - FADE_DISTANCE){
			CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist);
			ent->bDistanceFade = true;
			return VIS_INVISIBLE;
		}

		if(mi->m_drawLast){
			CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist);
			ent->bDistanceFade = false;
			return VIS_INVISIBLE;
		}
		ent->bDistanceFade = false;
		return VIS_VISIBLE;
	}

	if(mi->m_noFade){
		ent->DeleteRwObject();
		return VIS_INVISIBLE;
	}


	// get faded atomic
	a = mi->GetFirstAtomicFromDistance(lodDist - FADE_DISTANCE);
	if(a == nil){
		if(ent->bStreamBIGBuilding && lodDist-STREAM_DISTANCE-FADE_DISTANCE < mi->GetLodDistance(0) && request){
			return ent->GetIsOnScreen() ? VIS_STREAMME : VIS_INVISIBLE;
		}else{
			ent->DeleteRwObject();
			return VIS_INVISIBLE;
		}
	}

	// Fade...
	if(ent->m_rwObject == nil)
		ent->CreateRwObject();
	assert(ent->m_rwObject);
	RpAtomic *rwobj = (RpAtomic*)ent->m_rwObject;
	if(RpAtomicGetGeometry(a) != RpAtomicGetGeometry(rwobj))
		RpAtomicSetGeometry(rwobj, RpAtomicGetGeometry(a), rpATOMICSAMEBOUNDINGSPHERE); // originally 5 (mistake?)
	mi->IncreaseAlpha();
	if(!ent->IsVisible() || !ent->GetIsOnScreenComplex() || ent->IsEntityOccluded()){
		mi->m_alpha = 255;
		return VIS_INVISIBLE;
	}
	CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist);
	ent->bDistanceFade = true;
	return VIS_INVISIBLE;
}

void
CRenderer::ConstructRenderList(void)
{
#ifdef _3DS
	CrowdDrawBudget3DS::BeginFrame();
#endif
	COcclusion::ProcessBeforeRendering();
	ms_vecCameraPosition = TheCamera.GetPosition();

	// unused
	TestCloseThings = 0;
	TestBigThings = 0;

	ScanWorld();

	// LCS: mobile has a bunch of code after this,
}

void
LimitFrustumVector(CVector &vec1, const CVector &vec2, float l)
{
	float f;
	f = (l - vec2.z) / (vec1.z - vec2.z);
	vec1.x = f*(vec1.x - vec2.x) + vec2.x;
	vec1.y = f*(vec1.y - vec2.y) + vec2.y;
	vec1.z = f*(vec1.z - vec2.z) + vec2.z;
}

enum Corners
{
	CORNER_CAM = 0,
	CORNER_FAR_TOPLEFT,
	CORNER_FAR_TOPRIGHT,
	CORNER_FAR_BOTRIGHT,
	CORNER_FAR_BOTLEFT,
	CORNER_LOD_LEFT,
	CORNER_LOD_RIGHT,
	CORNER_PRIO_LEFT,
	CORNER_PRIO_RIGHT,
};

void
CRenderer::ScanWorld(void)
{
	float f = RwCameraGetFarClipPlane(TheCamera.m_pRwCamera);
	RwV2d vw = *RwCameraGetViewWindow(TheCamera.m_pRwCamera);
	CVector vectors[9];
	RwMatrix *cammatrix;
	RwV2d poly[3];

	memset(vectors, 0, sizeof(vectors));
	vectors[CORNER_FAR_TOPLEFT].x = -vw.x * f;
	vectors[CORNER_FAR_TOPLEFT].y = vw.y * f;
	vectors[CORNER_FAR_TOPLEFT].z = f;
	vectors[CORNER_FAR_TOPRIGHT].x = vw.x * f;
	vectors[CORNER_FAR_TOPRIGHT].y = vw.y * f;
	vectors[CORNER_FAR_TOPRIGHT].z = f;
	vectors[CORNER_FAR_BOTRIGHT].x = vw.x * f;
	vectors[CORNER_FAR_BOTRIGHT].y = -vw.y * f;
	vectors[CORNER_FAR_BOTRIGHT].z = f;
	vectors[CORNER_FAR_BOTLEFT].x = -vw.x * f;
	vectors[CORNER_FAR_BOTLEFT].y = -vw.y * f;
	vectors[CORNER_FAR_BOTLEFT].z = f;

	cammatrix = RwFrameGetMatrix(RwCameraGetFrame(TheCamera.m_pRwCamera));

	m_pFirstPersonVehicle = nil;
	CVisibilityPlugins::InitAlphaEntityList();
	CWorld::AdvanceCurrentScanCode();

	// unused
	static CVector prevPos;
	static CVector prevFwd;
	static bool smallMovement;
	smallMovement = (TheCamera.GetPosition() - prevPos).MagnitudeSqr() < SQR(4.0f) &&
		DotProduct(TheCamera.GetForward(), prevFwd) > 0.98f;
	prevPos = TheCamera.GetPosition();
	prevFwd = TheCamera.GetForward();

	if(cammatrix->at.z > 0.0f){
		// looking up, bottom corners are further away
		vectors[CORNER_LOD_LEFT] = vectors[CORNER_FAR_BOTLEFT] * LOD_DISTANCE/f;
		vectors[CORNER_LOD_RIGHT] = vectors[CORNER_FAR_BOTRIGHT] * LOD_DISTANCE/f;
	}else{
		// looking down, top corners are further away
		vectors[CORNER_LOD_LEFT] = vectors[CORNER_FAR_TOPLEFT] * LOD_DISTANCE/f;
		vectors[CORNER_LOD_RIGHT] = vectors[CORNER_FAR_TOPRIGHT] * LOD_DISTANCE/f;
	}
	vectors[CORNER_PRIO_LEFT].x = vectors[CORNER_LOD_LEFT].x * 0.2f;
	vectors[CORNER_PRIO_LEFT].y = vectors[CORNER_LOD_LEFT].y * 0.2f;
	vectors[CORNER_PRIO_LEFT].z = vectors[CORNER_LOD_LEFT].z;
	vectors[CORNER_PRIO_RIGHT].x = vectors[CORNER_LOD_RIGHT].x * 0.2f;
	vectors[CORNER_PRIO_RIGHT].y = vectors[CORNER_LOD_RIGHT].y * 0.2f;
	vectors[CORNER_PRIO_RIGHT].z = vectors[CORNER_LOD_RIGHT].z;
	RwV3dTransformPoints(vectors, vectors, 9, cammatrix);

	m_loadingPriority = false;
	#ifdef _3DS
	if((CameraPreset3DS::Display().moving || CameraPreset3DS::TopDownFrustum(cammatrix->at.z))){
#else
	if(TheCamera.Cams[TheCamera.ActiveCam].Mode == CCam::MODE_TOPDOWN ||
#ifdef FIX_BUGS
	   TheCamera.Cams[TheCamera.ActiveCam].Mode == CCam::MODE_GTACLASSIC ||
#endif
	   TheCamera.Cams[TheCamera.ActiveCam].Mode == CCam::MODE_TOP_DOWN_PED){
#endif
#ifdef _3DS
		CVector footprintCorners[5];footprintCorners[0]=vectors[CORNER_CAM];
		const float scanScale=Min(f,LOD_DISTANCE)/f;
		for(int i=1;i<5;++i)footprintCorners[i]=vectors[CORNER_CAM]+(vectors[i]-vectors[CORNER_CAM])*scanScale;
		CameraPreset3DS::Point2 hull[10];RwV2d sectorHull[10];
		const int count=CameraPreset3DS::FrustumHull(footprintCorners,hull);
		for(int i=0;i<count;++i){sectorHull[i].x=CWorld::GetSectorX(hull[i].x);sectorHull[i].y=CWorld::GetSectorY(hull[i].y);}
		if(count>=3)ScanSectorPoly(sectorHull,count,ScanSectorList);
#ifndef GTA_WORLDSTREAM
		if(CGame::currLevel!=LEVEL_GENERIC)ScanBigBuildingList(CWorld::GetBigBuildingList(CGame::currLevel));
		ScanBigBuildingList(CWorld::GetBigBuildingList(LEVEL_GENERIC));
#endif
#else
		CRect rect;
		int x1, x2, y1, y2;
		LimitFrustumVector(vectors[CORNER_FAR_TOPLEFT], vectors[CORNER_CAM], -100.0f);
		rect.ContainPoint(vectors[CORNER_FAR_TOPLEFT]);
		LimitFrustumVector(vectors[CORNER_FAR_TOPRIGHT], vectors[CORNER_CAM], -100.0f);
		rect.ContainPoint(vectors[CORNER_FAR_TOPRIGHT]);
		LimitFrustumVector(vectors[CORNER_FAR_BOTRIGHT], vectors[CORNER_CAM], -100.0f);
		rect.ContainPoint(vectors[CORNER_FAR_BOTRIGHT]);
		LimitFrustumVector(vectors[CORNER_FAR_BOTLEFT], vectors[CORNER_CAM], -100.0f);
		rect.ContainPoint(vectors[CORNER_FAR_BOTLEFT]);
		x1 = CWorld::GetSectorIndexX(rect.left);
		if(x1 < 0) x1 = 0;
		x2 = CWorld::GetSectorIndexX(rect.right);
		if(x2 >= NUMSECTORS_X-1) x2 = NUMSECTORS_X-1;
		y1 = CWorld::GetSectorIndexY(rect.top);
		if(y1 < 0) y1 = 0;
		y2 = CWorld::GetSectorIndexY(rect.bottom);
		if(y2 >= NUMSECTORS_Y-1) y2 = NUMSECTORS_Y-1;
		for(; x1 <= x2; x1++)
			for(int y = y1; y <= y2; y++)
				ScanSectorList(CWorld::GetSector(x1, y)->m_lists);
#endif
	}else{
#ifdef GTA_TRAIN
		CVehicle *train = FindPlayerTrain();
		if(train && train->GetPosition().z < 0.0f){
			poly[0].x = CWorld::GetSectorX(vectors[CORNER_CAM].x);
			poly[0].y = CWorld::GetSectorY(vectors[CORNER_CAM].y);
			poly[1].x = CWorld::GetSectorX(vectors[CORNER_LOD_LEFT].x);
			poly[1].y = CWorld::GetSectorY(vectors[CORNER_LOD_LEFT].y);
			poly[2].x = CWorld::GetSectorX(vectors[CORNER_LOD_RIGHT].x);
			poly[2].y = CWorld::GetSectorY(vectors[CORNER_LOD_RIGHT].y);
			ScanSectorPoly(poly, 3, ScanSectorList_Subway);
		}else
#endif
		{
			if(f > LOD_DISTANCE){
				// priority
				poly[0].x = CWorld::GetSectorX(vectors[CORNER_CAM].x);
				poly[0].y = CWorld::GetSectorY(vectors[CORNER_CAM].y);
				poly[1].x = CWorld::GetSectorX(vectors[CORNER_PRIO_LEFT].x);
				poly[1].y = CWorld::GetSectorY(vectors[CORNER_PRIO_LEFT].y);
				poly[2].x = CWorld::GetSectorX(vectors[CORNER_PRIO_RIGHT].x);
				poly[2].y = CWorld::GetSectorY(vectors[CORNER_PRIO_RIGHT].y);
				ScanSectorPoly(poly, 3, ScanSectorList_Priority);

				// below LOD
				poly[0].x = CWorld::GetSectorX(vectors[CORNER_CAM].x);
				poly[0].y = CWorld::GetSectorY(vectors[CORNER_CAM].y);
				poly[1].x = CWorld::GetSectorX(vectors[CORNER_LOD_LEFT].x);
				poly[1].y = CWorld::GetSectorY(vectors[CORNER_LOD_LEFT].y);
				poly[2].x = CWorld::GetSectorX(vectors[CORNER_LOD_RIGHT].x);
				poly[2].y = CWorld::GetSectorY(vectors[CORNER_LOD_RIGHT].y);
				ScanSectorPoly(poly, 3, ScanSectorList);
			}else{
				poly[0].x = CWorld::GetSectorX(vectors[CORNER_CAM].x);
				poly[0].y = CWorld::GetSectorY(vectors[CORNER_CAM].y);
				poly[1].x = CWorld::GetSectorX(vectors[CORNER_FAR_TOPLEFT].x);
				poly[1].y = CWorld::GetSectorY(vectors[CORNER_FAR_TOPLEFT].y);
				poly[2].x = CWorld::GetSectorX(vectors[CORNER_FAR_TOPRIGHT].x);
				poly[2].y = CWorld::GetSectorY(vectors[CORNER_FAR_TOPRIGHT].y);
				ScanSectorPoly(poly, 3, ScanSectorList);
			}

#ifndef GTA_WORLDSTREAM
			// TODO(LCS): may be better to have this code with gbPreviewCity
#ifdef NO_ISLAND_LOADING
			if (FrontEndMenuManager.m_PrefsIslandLoading == CMenuManager::ISLAND_LOADING_HIGH) {
				ScanBigBuildingList(CWorld::GetBigBuildingList(LEVEL_BEACH));
				ScanBigBuildingList(CWorld::GetBigBuildingList(LEVEL_MAINLAND));
			} else 
#endif
			{
#ifdef FIX_BUGS
			if(CCollision::ms_collisionInMemory != LEVEL_GENERIC)
#endif
				ScanBigBuildingList(CWorld::GetBigBuildingList(CGame::currLevel));
			}
			ScanBigBuildingList(CWorld::GetBigBuildingList(LEVEL_GENERIC));
#endif
		}
	}
}

void
CRenderer::RequestObjectsInFrustum(void)
{
	float f = RwCameraGetFarClipPlane(TheCamera.m_pRwCamera);
	RwV2d vw = *RwCameraGetViewWindow(TheCamera.m_pRwCamera);
	CVector vectors[9];
	RwMatrix *cammatrix;
	RwV2d poly[3];

	memset(vectors, 0, sizeof(vectors));
	vectors[CORNER_FAR_TOPLEFT].x = -vw.x * f;
	vectors[CORNER_FAR_TOPLEFT].y = vw.y * f;
	vectors[CORNER_FAR_TOPLEFT].z = f;
	vectors[CORNER_FAR_TOPRIGHT].x = vw.x * f;
	vectors[CORNER_FAR_TOPRIGHT].y = vw.y * f;
	vectors[CORNER_FAR_TOPRIGHT].z = f;
	vectors[CORNER_FAR_BOTRIGHT].x = vw.x * f;
	vectors[CORNER_FAR_BOTRIGHT].y = -vw.y * f;
	vectors[CORNER_FAR_BOTRIGHT].z = f;
	vectors[CORNER_FAR_BOTLEFT].x = -vw.x * f;
	vectors[CORNER_FAR_BOTLEFT].y = -vw.y * f;
	vectors[CORNER_FAR_BOTLEFT].z = f;

	cammatrix = RwFrameGetMatrix(RwCameraGetFrame(TheCamera.m_pRwCamera));

	CWorld::AdvanceCurrentScanCode();
	ms_vecCameraPosition = TheCamera.GetPosition();

	if(cammatrix->at.z > 0.0f){
		// looking up, bottom corners are further away
		vectors[CORNER_LOD_LEFT] = vectors[CORNER_FAR_BOTLEFT] * LOD_DISTANCE/f;
		vectors[CORNER_LOD_RIGHT] = vectors[CORNER_FAR_BOTRIGHT] * LOD_DISTANCE/f;
	}else{
		// looking down, top corners are further away
		vectors[CORNER_LOD_LEFT] = vectors[CORNER_FAR_TOPLEFT] * LOD_DISTANCE/f;
		vectors[CORNER_LOD_RIGHT] = vectors[CORNER_FAR_TOPRIGHT] * LOD_DISTANCE/f;
	}
	vectors[CORNER_PRIO_LEFT].x = vectors[CORNER_LOD_LEFT].x * 0.2f;
	vectors[CORNER_PRIO_LEFT].y = vectors[CORNER_LOD_LEFT].y * 0.2f;
	vectors[CORNER_PRIO_LEFT].z = vectors[CORNER_LOD_LEFT].z;
	vectors[CORNER_PRIO_RIGHT].x = vectors[CORNER_LOD_RIGHT].x * 0.2f;
	vectors[CORNER_PRIO_RIGHT].y = vectors[CORNER_LOD_RIGHT].y * 0.2f;
	vectors[CORNER_PRIO_RIGHT].z = vectors[CORNER_LOD_RIGHT].z;
	RwV3dTransformPoints(vectors, vectors, 9, cammatrix);

	#ifdef _3DS
	if((CameraPreset3DS::Display().moving || CameraPreset3DS::TopDownFrustum(cammatrix->at.z))){
#else
	if(TheCamera.Cams[TheCamera.ActiveCam].Mode == CCam::MODE_TOPDOWN ||
#ifdef FIX_BUGS
	   TheCamera.Cams[TheCamera.ActiveCam].Mode == CCam::MODE_GTACLASSIC ||
#endif
	   TheCamera.Cams[TheCamera.ActiveCam].Mode == CCam::MODE_TOP_DOWN_PED){
#endif
#ifdef _3DS
		CVector footprintCorners[5];footprintCorners[0]=vectors[CORNER_CAM];
		const float scanScale=Min(f,LOD_DISTANCE)/f;
		for(int i=1;i<5;++i)footprintCorners[i]=vectors[CORNER_CAM]+(vectors[i]-vectors[CORNER_CAM])*scanScale;
		CameraPreset3DS::Point2 hull[10];RwV2d sectorHull[10];
		const int count=CameraPreset3DS::FrustumHull(footprintCorners,hull);
		for(int i=0;i<count;++i){sectorHull[i].x=CWorld::GetSectorX(hull[i].x);sectorHull[i].y=CWorld::GetSectorY(hull[i].y);}
		if(count>=3)ScanSectorPoly(sectorHull,count,ScanSectorList_RequestModels);
#else
		CRect rect;
		int x1, x2, y1, y2;
		LimitFrustumVector(vectors[CORNER_FAR_TOPLEFT], vectors[CORNER_CAM], -100.0f);
		rect.ContainPoint(vectors[CORNER_FAR_TOPLEFT]);
		LimitFrustumVector(vectors[CORNER_FAR_TOPRIGHT], vectors[CORNER_CAM], -100.0f);
		rect.ContainPoint(vectors[CORNER_FAR_TOPRIGHT]);
		LimitFrustumVector(vectors[CORNER_FAR_BOTRIGHT], vectors[CORNER_CAM], -100.0f);
		rect.ContainPoint(vectors[CORNER_FAR_BOTRIGHT]);
		LimitFrustumVector(vectors[CORNER_FAR_BOTLEFT], vectors[CORNER_CAM], -100.0f);
		rect.ContainPoint(vectors[CORNER_FAR_BOTLEFT]);
		x1 = CWorld::GetSectorIndexX(rect.left);
		if(x1 < 0) x1 = 0;
		x2 = CWorld::GetSectorIndexX(rect.right);
		if(x2 >= NUMSECTORS_X-1) x2 = NUMSECTORS_X-1;
		y1 = CWorld::GetSectorIndexY(rect.top);
		if(y1 < 0) y1 = 0;
		y2 = CWorld::GetSectorIndexY(rect.bottom);
		if(y2 >= NUMSECTORS_Y-1) y2 = NUMSECTORS_Y-1;
		for(; x1 <= x2; x1++)
			for(int y = y1; y <= y2; y++)
				ScanSectorList_RequestModels(CWorld::GetSector(x1, y)->m_lists);
#endif
	}else{
		poly[0].x = CWorld::GetSectorX(vectors[CORNER_CAM].x);
		poly[0].y = CWorld::GetSectorY(vectors[CORNER_CAM].y);
		poly[1].x = CWorld::GetSectorX(vectors[CORNER_LOD_LEFT].x);
		poly[1].y = CWorld::GetSectorY(vectors[CORNER_LOD_LEFT].y);
		poly[2].x = CWorld::GetSectorX(vectors[CORNER_LOD_RIGHT].x);
		poly[2].y = CWorld::GetSectorY(vectors[CORNER_LOD_RIGHT].y);
		ScanSectorPoly(poly, 3, ScanSectorList_RequestModels);
	}
}

bool
CEntity::SetupLighting(void)
{
	return false;
}

void
CEntity::RemoveLighting(bool)
{
}

bool
CPed::SetupLighting(void)
{
	ActivateDirectional();
	SetAmbientColoursForPedsCarsAndObjects();

#ifndef MASTER
	// Originally this was being called through iteration of Sectors, but putting it here is better.
	if (GetDebugDisplay() != 0 && !IsPlayer())
		DebugRenderOnePedText();
#endif

	if (bRenderScorched) {
		WorldReplaceNormalLightsWithScorched(Scene.world, 0.1f);
	} else {
		// Note that this lightMult is only affected by LIGHT_DARKEN. If there's no LIGHT_DARKEN, it will be 1.0.
		float lightMult = CPointLights::GenerateLightsAffectingObject(&GetPosition());
		if (lightMult != 1.0f) {
			SetAmbientAndDirectionalColours(lightMult);
			return true;
		}
	}
	return false;
}

void
CPed::RemoveLighting(bool reset)
{
	if (!bRenderScorched) {
		CRenderer::RemoveVehiclePedLights(this, reset);
		if (reset)
			ReSetAmbientAndDirectionalColours();
	}
	SetAmbientColours();
	DeActivateDirectional();
}

float
CalcNewDelta(RwV2d *a, RwV2d *b)
{
	return (b->x - a->x) / (b->y - a->y);
}

#ifdef FIX_BUGS
#define TOINT(x) ((int)Floor(x))
#else
#define TOINT(x) ((int)(x))
#endif

void
CRenderer::ScanSectorPoly(RwV2d *poly, int32 numVertices, void (*scanfunc)(CPtrList *))
{
	float miny, maxy;
	int y, yend;
	int x, xstart, xend;
	int i;
	int a1, a2, b1, b2;
	float deltaA, deltaB;
	float xA, xB;

	miny = poly[0].y;
	maxy = poly[0].y;
	a2 = 0;
	xstart = 9999;
	xend = -9999;

	for(i = 1; i < numVertices; i++){
		if(poly[i].y > maxy)
			maxy = poly[i].y;
		if(poly[i].y < miny){
			miny = poly[i].y;
			a2 = i;
		}
	}
	y = TOINT(miny);
	yend = TOINT(maxy);

	// Go left in poly to find first edge b
	b2 = a2;
	for(i = 0; i < numVertices; i++){
		b1 = b2--;
		if(b2 < 0) b2 = numVertices-1;
		if(poly[b1].x < xstart)
			xstart = TOINT(poly[b1].x);
		if(TOINT(poly[b1].y) != TOINT(poly[b2].y))
			break;
	}
	// Go right to find first edge a
	for(i = 0; i < numVertices; i++){
		a1 = a2++;
		if(a2 == numVertices) a2 = 0;
		if(poly[a1].x > xend)
			xend = TOINT(poly[a1].x);
		if(TOINT(poly[a1].y) != TOINT(poly[a2].y))
			break;
	}

	// prestep x1 and x2 to next integer y
	deltaA = CalcNewDelta(&poly[a1], &poly[a2]);
	xA = deltaA * (Ceil(poly[a1].y) - poly[a1].y) + poly[a1].x;
	deltaB = CalcNewDelta(&poly[b1], &poly[b2]);
	xB = deltaB * (Ceil(poly[b1].y) - poly[b1].y) + poly[b1].x;

	if(y != yend){
		if(deltaB < 0.0f && TOINT(xB) < xstart)
			xstart = TOINT(xB);
		if(deltaA >= 0.0f && TOINT(xA) > xend)
			xend = TOINT(xA);
	}

	while(y <= yend && y < NUMSECTORS_Y){
		// scan one x-line
		if(y >= 0 && xstart < NUMSECTORS_X)
			for(x = xstart; x <= xend && x != NUMSECTORS_X; x++)
				if(x >= 0)
					scanfunc(CWorld::GetSector(x, y)->m_lists);

		// advance one scan line
		y++;
		xA += deltaA;
		xB += deltaB;

		// update left side
		if(y == TOINT(poly[b2].y)){
			// reached end of edge
			if(y == yend){
				if(deltaB < 0.0f){
					do{
						xstart = TOINT(poly[b2--].x);
						if(b2 < 0) b2 = numVertices-1;
					}while(xstart > TOINT(poly[b2].x));
				}else
					xstart = TOINT(xB - deltaB);
			}else{
				// switch edges
				if(deltaB < 0.0f)
					xstart = TOINT(poly[b2].x);
				else
					xstart = TOINT(xB - deltaB);
				do{
					b1 = b2--;
					if(b2 < 0) b2 = numVertices-1;
					if(TOINT(poly[b1].x) < xstart)
						xstart = TOINT(poly[b1].x);
				}while(y == TOINT(poly[b2].y));
				deltaB = CalcNewDelta(&poly[b1], &poly[b2]);
				xB = deltaB * (Ceil(poly[b1].y) - poly[b1].y) + poly[b1].x;
				if(deltaB < 0.0f && TOINT(xB) < xstart)
					xstart = TOINT(xB);
			}
		}else{
			if(deltaB < 0.0f)
				xstart = TOINT(xB);
			else
				xstart = TOINT(xB - deltaB);
		}

		// update right side
		if(y == TOINT(poly[a2].y)){
			// reached end of edge
			if(y == yend){
				if(deltaA < 0.0f)
					xend = TOINT(xA - deltaA);
				else{
					do{
						xend = TOINT(poly[a2++].x);
						if(a2 == numVertices) a2 = 0;
					}while(xend < TOINT(poly[a2].x));
				}
			}else{
				// switch edges
				if(deltaA < 0.0f)
					xend = TOINT(xA - deltaA);
				else
					xend = TOINT(poly[a2].x);
				do{
					a1 = a2++;
					if(a2 == numVertices) a2 = 0;
					if(TOINT(poly[a1].x) > xend)
						xend = TOINT(poly[a1].x);
				}while(y == TOINT(poly[a2].y));
				deltaA = CalcNewDelta(&poly[a1], &poly[a2]);
				xA = deltaA * (Ceil(poly[a1].y) - poly[a1].y) + poly[a1].x;
				if(deltaA >= 0.0f && TOINT(xA) > xend)
					xend = TOINT(xA);
			}
		}else{
			if(deltaA < 0.0f)
				xend = TOINT(xA - deltaA);
			else
				xend = TOINT(xA);
		}
	}
}

// probably didn't exist as a separate function in LCS
void
CRenderer::InsertEntityIntoList(CEntity *ent)
{
#ifdef FIX_BUGS
	if (!ent->m_rwObject) return;
#endif

	if(ent->IsVehicle() || ent->bIsVehicle || ent->IsPed() || ent->bIsPed)
		ms_aVisibleVehiclePtrs[ms_nNoOfVisibleVehicles++] = ent;
#ifdef NEW_RENDERER
	else if(!gbPreviewCity && ent->IsBuilding())
		ms_aVisibleBuildingPtrs[ms_nNoOfVisibleBuildings++] = ent;
#endif
	else
		ms_aVisibleEntityPtrs[ms_nNoOfVisibleEntities++] = ent;
#if defined(VIS_DISTANCE_ALPHA) && defined(FIX_BUGS)
	// this flag can only be trusted if entity is in alpha list
	// unfortunately we're checking it in other cases too, which is bad.
	// no idea why this isn't a problem on android
	ent->bDistanceFade = false;
#endif
}

void
CRenderer::AddVisibleEntity(CEntity *ent)
{
#ifdef NEW_RENDERER
	if(!gbPreviewCity && ent->IsBuilding())
		ms_aVisibleBuildingPtrs[ms_nNoOfVisibleBuildings++] = ent;
	else
#endif
		ms_aVisibleEntityPtrs[ms_nNoOfVisibleEntities++] = ent;
	ent->bOffscreen = false;
}

void
CRenderer::ScanBigBuildingList(CPtrList &list)
{
	CPtrNode *node;
	CEntity *ent;
	int vis;

	int f = CTimer::GetFrameCounter() & 6;
	for(node = list.first; node; node = node->next){
		ent = (CEntity*)node->item;
		if(ent->bOffscreen || (ent->m_randomSeed&6) != f){
			ent->bOffscreen = true;
			vis = SetupBigBuildingVisibility(ent);
		}else
			vis = ent->bOffscreen ? VIS_INVISIBLE : VIS_VISIBLE;
		if(ent->bMakeVisible){
			ent->bMakeVisible = false;
			ent->bIsVisible = true;
		}
		switch(vis){
		case VIS_VISIBLE:
			AddVisibleEntity(ent);
			break;
		case VIS_STREAMME:
			if(!CStreaming::ms_disableStreaming)
				CStreaming::RequestModel(ent->GetModelIndex(), 0);
			break;
		}
	}
}

void
CRenderer::ScanSectorList(CPtrList *lists)
{
	CPtrNode *node;
	CPtrList *list;
	CEntity *ent;
	int i;
	float dx, dy;

	int numLists = NUMSECTORENTITYLISTS;
#ifdef GTA_WORLDSTREAM
	if(gbPreviewCity){
		numLists -= ENTITYLIST_UNKNOWN;
		lists += ENTITYLIST_UNKNOWN;
	}
#endif

	for(i = 0; i < numLists; i++){
		list = &lists[i];
		for(node = list->first; node; node = node->next){
			ent = (CEntity*)node->item;
			if(ent->m_scanCode == CWorld::GetCurrentScanCode())
				continue;	// already seen
			ent->m_scanCode = CWorld::GetCurrentScanCode();
			ent->bOffscreen = false;

			int vis = SetupEntityVisibility(ent);
			if(ent->bMakeVisible){
				ent->bMakeVisible = false;
				ent->bIsVisible = true;
			}
			switch(vis){
			case VIS_VISIBLE:
				InsertEntityIntoList(ent);
				break;
			case VIS_INVISIBLE:
				if(!IsGlass(ent->GetModelIndex()))
					break;
				// fall through
			case VIS_OFFSCREEN:
				ent->bOffscreen = true;
				dx = ms_vecCameraPosition.x - ent->GetPosition().x;
				dy = ms_vecCameraPosition.y - ent->GetPosition().y;
				if(dx > -30.0f && dx < 30.0f &&
				   dy > -30.0f && dy < 30.0f &&
				   ms_nNoOfInVisibleEntities < NUMINVISIBLEENTITIES - 1)
					ms_aInVisibleEntityPtrs[ms_nNoOfInVisibleEntities++] = ent;
				break;
			case VIS_STREAMME:
				if(!CStreaming::ms_disableStreaming)
					if(!m_loadingPriority || CStreaming::ms_numModelsRequested < 10)
						CStreaming::RequestModel(ent->GetModelIndex(), 0);
				break;
			}
		}
	}
}

void
CRenderer::ScanSectorList_Priority(CPtrList *lists)
{
	CPtrNode *node;
	CPtrList *list;
	CEntity *ent;
	int i;
	float dx, dy;

	int numLists = NUMSECTORENTITYLISTS;
#ifdef GTA_WORLDSTREAM
	if(gbPreviewCity){
		numLists -= ENTITYLIST_UNKNOWN;
		lists += ENTITYLIST_UNKNOWN;
	}
#endif

	for(i = 0; i < numLists; i++){
		list = &lists[i];
		for(node = list->first; node; node = node->next){
			ent = (CEntity*)node->item;
			if(ent->m_scanCode == CWorld::GetCurrentScanCode())
				continue;	// already seen
			ent->m_scanCode = CWorld::GetCurrentScanCode();
			ent->bOffscreen = false;

			int vis = SetupEntityVisibility(ent);
			if(ent->bMakeVisible){
				ent->bMakeVisible = false;
				ent->bIsVisible = true;
			}
			switch(vis){
			case VIS_VISIBLE:
				InsertEntityIntoList(ent);
				break;
			case VIS_INVISIBLE:
				if(!IsGlass(ent->GetModelIndex()))
					break;
				// fall through
			case VIS_OFFSCREEN:
				ent->bOffscreen = true;
				dx = ms_vecCameraPosition.x - ent->GetPosition().x;
				dy = ms_vecCameraPosition.y - ent->GetPosition().y;
				if(dx > -30.0f && dx < 30.0f &&
				   dy > -30.0f && dy < 30.0f &&
				   ms_nNoOfInVisibleEntities < NUMINVISIBLEENTITIES - 1)
					ms_aInVisibleEntityPtrs[ms_nNoOfInVisibleEntities++] = ent;
				break;
			case VIS_STREAMME:
				if(!CStreaming::ms_disableStreaming){
					CStreaming::RequestModel(ent->GetModelIndex(), 0);
					if(CStreaming::ms_aInfoForModel[ent->GetModelIndex()].m_loadState != STREAMSTATE_LOADED)
						m_loadingPriority = true;
				}
				break;
			}
		}
	}
}

#ifdef GTA_TRAIN
void
CRenderer::ScanSectorList_Subway(CPtrList *lists)
{
	CPtrNode *node;
	CPtrList *list;
	CEntity *ent;
	int i;
	float dx, dy;

	int numLists = NUMSECTORENTITYLISTS;
#ifdef GTA_WORLDSTREAM
	if(gbPreviewCity){
		numLists -= ENTITYLIST_UNKNOWN;
		lists += ENTITYLIST_UNKNOWN;
	}
#endif

	for(i = 0; i < numLists; i++){
		list = &lists[i];
		for(node = list->first; node; node = node->next){
			ent = (CEntity*)node->item;
			if(ent->m_scanCode == CWorld::GetCurrentScanCode())
				continue;	// already seen
			ent->m_scanCode = CWorld::GetCurrentScanCode();
			ent->bOffscreen = false;
			switch(SetupEntityVisibility(ent)){
			case VIS_VISIBLE:
				InsertEntityIntoList(ent);
				break;
			case VIS_OFFSCREEN:
				ent->bOffscreen = true;
				dx = ms_vecCameraPosition.x - ent->GetPosition().x;
				dy = ms_vecCameraPosition.y - ent->GetPosition().y;
				if(dx > -30.0f && dx < 30.0f &&
				   dy > -30.0f && dy < 30.0f &&
				   ms_nNoOfInVisibleEntities < NUMINVISIBLEENTITIES - 1)
					ms_aInVisibleEntityPtrs[ms_nNoOfInVisibleEntities++] = ent;
				break;
			}
		}
	}
}
#endif

void
CRenderer::ScanSectorList_RequestModels(CPtrList *lists)
{
	CPtrNode *node;
	CPtrList *list;
	CEntity *ent;
	int i;

	int numLists = NUMSECTORENTITYLISTS;
#ifdef GTA_WORLDSTREAM
	if(gbPreviewCity){
		numLists -= ENTITYLIST_UNKNOWN;
		lists += ENTITYLIST_UNKNOWN;
	}
#endif

	for(i = 0; i < numLists; i++){
		list = &lists[i];
		for(node = list->first; node; node = node->next){
			ent = (CEntity*)node->item;
			if(ent->m_scanCode == CWorld::GetCurrentScanCode())
				continue;	// already seen
			ent->m_scanCode = CWorld::GetCurrentScanCode();
			if(ShouldModelBeStreamed(ent, ms_vecCameraPosition))
				CStreaming::RequestModel(ent->GetModelIndex(), 0);
		}
	}
}

// Put big buildings in front
// This seems pointless because the sector lists shouldn't have big buildings in the first place
void
CRenderer::SortBIGBuildings(void)
{
	int x, y;
	for(y = 0; y < NUMSECTORS_Y; y++)
		for(x = 0; x < NUMSECTORS_X; x++){
			SortBIGBuildingsForSectorList(&CWorld::GetSector(x, y)->m_lists[ENTITYLIST_BUILDINGS]);
			SortBIGBuildingsForSectorList(&CWorld::GetSector(x, y)->m_lists[ENTITYLIST_BUILDINGS_OVERLAP]);
		}
}

void
CRenderer::SortBIGBuildingsForSectorList(CPtrList *list)
{
	CPtrNode *node;
	CEntity *ent;

	for(node = list->first; node; node = node->next){
		ent = (CEntity*)node->item;
		if(ent->bIsBIGBuilding){
			list->RemoveNode(node);
			list->InsertNode(node);
		}
	}
}

bool
CRenderer::ShouldModelBeStreamed(CEntity *ent, const CVector &campos)
{
	if(!IsAreaVisible(ent->m_area))
		return false;
	CTimeModelInfo *mi = (CTimeModelInfo *)CModelInfo::GetModelInfo(ent->GetModelIndex());
	if(!ent->IsObject() && !ent->IsDummy()){
		if(mi->GetModelType() == MITYPE_TIME)
			if(!CClock::GetIsTimeInRange(mi->GetTimeOn(), mi->GetTimeOff()))
				return false;
		// LCS(TODO): cWorldStream::pDynamic, but just returns 0 anyway
	}
	float dist = (ent->GetPosition() - campos).Magnitude();
	if(mi->m_noFade)
		return dist - STREAM_DISTANCE < mi->GetLargestLodDistance();
	else
		return dist - FADE_DISTANCE - STREAM_DISTANCE < mi->GetLargestLodDistance();
}

void
CRenderer::RemoveVehiclePedLights(CEntity *ent, bool reset)
{
	if(!ent->bRenderScorched){
		CPointLights::RemoveLightsAffectingObject();
		if(reset)
			ReSetAmbientAndDirectionalColours();
	}
//	SetAmbientColours();
//	DeActivateDirectional();
}


#include "postfx.h"

static RwIm2DVertex Screen2EnvQuad[4];
static RwImVertexIndex EnvQuadIndices[6] = { 0, 1, 2, 0, 2, 3 };

static void
SetQuadVertices(RwRaster *env, RwRaster *screen, float z)
{
	uint32 width  = RwRasterGetWidth(env);
	uint32 height = RwRasterGetHeight(env);

	float zero, xmax, ymax;

	zero = -HALFPX;
	xmax = width - HALFPX;
	ymax = height - HALFPX;

	float recipz = 1.0f/z;
	float umax = (float)SCREEN_WIDTH/RwRasterGetWidth(screen);
	float vmax = (float)SCREEN_HEIGHT/RwRasterGetHeight(screen);

	RwIm2DVertexSetScreenX(&Screen2EnvQuad[0], zero);
	RwIm2DVertexSetScreenY(&Screen2EnvQuad[0], zero);
	RwIm2DVertexSetScreenZ(&Screen2EnvQuad[0], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Screen2EnvQuad[0], z);
	RwIm2DVertexSetRecipCameraZ(&Screen2EnvQuad[0], recipz);
	RwIm2DVertexSetU(&Screen2EnvQuad[0], 0.0f, recipz);
	RwIm2DVertexSetV(&Screen2EnvQuad[0], 0.0f, recipz);
	RwIm2DVertexSetIntRGBA(&Screen2EnvQuad[0], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Screen2EnvQuad[1], zero);
	RwIm2DVertexSetScreenY(&Screen2EnvQuad[1], ymax);
	RwIm2DVertexSetScreenZ(&Screen2EnvQuad[1], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Screen2EnvQuad[1], z);
	RwIm2DVertexSetRecipCameraZ(&Screen2EnvQuad[1], recipz);
	RwIm2DVertexSetU(&Screen2EnvQuad[1], 0.0f, recipz);
	RwIm2DVertexSetV(&Screen2EnvQuad[1], vmax, recipz);
	RwIm2DVertexSetIntRGBA(&Screen2EnvQuad[1], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Screen2EnvQuad[2], xmax);
	RwIm2DVertexSetScreenY(&Screen2EnvQuad[2], ymax);
	RwIm2DVertexSetScreenZ(&Screen2EnvQuad[2], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Screen2EnvQuad[2], z);
	RwIm2DVertexSetRecipCameraZ(&Screen2EnvQuad[2], recipz);
	RwIm2DVertexSetU(&Screen2EnvQuad[2], umax, recipz);
	RwIm2DVertexSetV(&Screen2EnvQuad[2], vmax, recipz);
	RwIm2DVertexSetIntRGBA(&Screen2EnvQuad[2], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Screen2EnvQuad[3], xmax);
	RwIm2DVertexSetScreenY(&Screen2EnvQuad[3], zero);
	RwIm2DVertexSetScreenZ(&Screen2EnvQuad[3], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Screen2EnvQuad[3], z);
	RwIm2DVertexSetRecipCameraZ(&Screen2EnvQuad[3], recipz);
	RwIm2DVertexSetU(&Screen2EnvQuad[3], umax, recipz);
	RwIm2DVertexSetV(&Screen2EnvQuad[3], 0.0f, recipz);
	RwIm2DVertexSetIntRGBA(&Screen2EnvQuad[3], 255, 255, 255, 255);
}

static RwIm2DVertex coronaVerts[4*4];
static RwImVertexIndex coronaIndices[6*4];
static int numCoronaVerts, numCoronaIndices;

static void
AddCorona(float x, float y, float sz)
{
	float nearz, recipz;
	RwIm2DVertex *v;
	nearz = RwIm2DGetNearScreenZ();
	float z = RwCameraGetNearClipPlane(RwCameraGetCurrentCamera());
	recipz = 1.0f/z;

	v = &coronaVerts[numCoronaVerts];
	RwIm2DVertexSetScreenX(&v[0], x);
	RwIm2DVertexSetScreenY(&v[0], y);
	RwIm2DVertexSetScreenZ(&v[0], z);
	RwIm2DVertexSetScreenZ(&v[0], nearz);
	RwIm2DVertexSetRecipCameraZ(&v[0], recipz);
	RwIm2DVertexSetU(&v[0], 0.0f, recipz);
	RwIm2DVertexSetV(&v[0], 0.0f, recipz);
	RwIm2DVertexSetIntRGBA(&v[0], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&v[1], x);
	RwIm2DVertexSetScreenY(&v[1], y + sz);
	RwIm2DVertexSetScreenZ(&v[1], z);
	RwIm2DVertexSetScreenZ(&v[1], nearz);
	RwIm2DVertexSetRecipCameraZ(&v[1], recipz);
	RwIm2DVertexSetU(&v[1], 0.0f, recipz);
	RwIm2DVertexSetV(&v[1], 1.0f, recipz);
	RwIm2DVertexSetIntRGBA(&v[1], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&v[2], x + sz);
	RwIm2DVertexSetScreenY(&v[2], y + sz);
	RwIm2DVertexSetScreenZ(&v[2], z);
	RwIm2DVertexSetScreenZ(&v[2], nearz);
	RwIm2DVertexSetRecipCameraZ(&v[2], recipz);
	RwIm2DVertexSetU(&v[2], 1.0f, recipz);
	RwIm2DVertexSetV(&v[2], 1.0f, recipz);
	RwIm2DVertexSetIntRGBA(&v[2], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&v[3], x + sz);
	RwIm2DVertexSetScreenY(&v[3], y);
	RwIm2DVertexSetScreenZ(&v[3], z);
	RwIm2DVertexSetScreenZ(&v[3], nearz);
	RwIm2DVertexSetRecipCameraZ(&v[3], recipz);
	RwIm2DVertexSetU(&v[3], 1.0f, recipz);
	RwIm2DVertexSetV(&v[3], 0.0f, recipz);
	RwIm2DVertexSetIntRGBA(&v[3], 255, 255, 255, 255);


	coronaIndices[numCoronaIndices++] = numCoronaVerts;
	coronaIndices[numCoronaIndices++] = numCoronaVerts + 1;
	coronaIndices[numCoronaIndices++] = numCoronaVerts + 2;
	coronaIndices[numCoronaIndices++] = numCoronaVerts;
	coronaIndices[numCoronaIndices++] = numCoronaVerts + 2;
	coronaIndices[numCoronaIndices++] = numCoronaVerts + 3;
	numCoronaVerts += 4;
}

#ifdef EXTENDED_PIPELINES
static void
DrawEnvMapCoronas(float heading)
{
	RwRaster *rt = RwTextureGetRaster(CustomPipes::EnvMapTex);
	const float BIG = 89.0f * RwRasterGetWidth(rt)/128.0f;
	const float SMALL = 38.0f * RwRasterGetHeight(rt)/128.0f;

	float x;
	numCoronaVerts = 0;
	numCoronaIndices = 0;
	x = (heading - PI)/TWOPI;// - 1.0f;
	x *= BIG+SMALL;
	AddCorona(x, 0.0f, BIG);	x += BIG;
	AddCorona(x, 12.0f, SMALL);	x += SMALL;
	AddCorona(x, 0.0f, BIG);	x += BIG;
	AddCorona(x, 12.0f, SMALL);	x += SMALL;

	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, RwTextureGetRaster(gpCoronaTexture[CCoronas::TYPE_STAR]));
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, coronaVerts, numCoronaVerts, coronaIndices, numCoronaIndices);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
}

void
CRenderer::GenerateEnvironmentMap(void)
{
	// We'll probably do this differently eventually
	// re-using all sorts of stuff here...

	CPostFX::GetBackBuffer(Scene.camera);

	RwCameraBeginUpdate(CustomPipes::EnvMapCam);

	// get current scene
	SetQuadVertices(RwTextureGetRaster(CustomPipes::EnvMapTex), CPostFX::pBackBuffer, RwCameraGetNearClipPlane(RwCameraGetCurrentCamera()));
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, CPostFX::pBackBuffer);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Screen2EnvQuad, 4, EnvQuadIndices, 6);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);

	// Draw coronas
	if(CustomPipes::VehiclePipeSwitch != CustomPipes::VEHICLEPIPE_MOBILE)
		DrawEnvMapCoronas(TheCamera.GetForward().Heading());

	RwCameraEndUpdate(CustomPipes::EnvMapCam);


	RwCameraBeginUpdate(Scene.camera);

	if(gbRenderDebugEnvMap){
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, RwTextureGetRaster(CustomPipes::EnvMapTex));
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, CustomPipes::EnvScreenQuad, 4, (RwImVertexIndex*)CustomPipes::QuadIndices, 6);
	}
}
#else
void
CRenderer::GenerateEnvironmentMap(void)
{
}
#endif
