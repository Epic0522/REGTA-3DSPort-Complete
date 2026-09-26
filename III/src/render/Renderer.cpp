#define WITHD3D
#include "common.h"
#include "../../../../common/3ds/CameraPresetTransition.h"

#include "main.h"
#include "Lights.h"
#include "ModelInfo.h"
#include "Treadable.h"
#include "Ped.h"
#include "Vehicle.h"
#include "Boat.h"
#include "Heli.h"
#include "Object.h"
#include "PathFind.h"
#include "Collision.h"
#include "VisibilityPlugins.h"
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
#include "PointLights.h"
#include "Renderer.h"
#include "Frontend.h"
#include "custompipes.h"
#include "Debug.h"
bool gbShowPedRoadGroups;
bool gbShowCarRoadGroups;
bool gbShowCollisionPolys;
bool gbShowCollisionLines;
bool gbShowCullZoneDebugStuff;
bool gbDisableZoneCull;	// not original
bool gbBigWhiteDebugLightSwitchedOn;

bool gbDontRenderBuildings;
bool gbDontRenderBigBuildings;
bool gbDontRenderPeds;
bool gbDontRenderObjects;
bool gbDontRenderVehicles;

int32 EntitiesRendered;
int32 EntitiesNotRendered;
int32 RenderedBigBuildings;
int32 RenderedBuildings;
int32 RenderedCars;
int32 RenderedPeds;
int32 RenderedObjects;
int32 RenderedDummies;
int32 TestedBigBuildings;
int32 TestedBuildings;
int32 TestedCars;
int32 TestedPeds;
int32 TestedObjects;
int32 TestedDummies;

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
#ifdef NEW_RENDERER
int32 CRenderer::ms_nNoOfVisibleVehicles;
CEntity *CRenderer::ms_aVisibleVehiclePtrs[NUMVISIBLEENTITIES];
int32 CRenderer::ms_nNoOfVisibleBuildings;
CEntity *CRenderer::ms_aVisibleBuildingPtrs[NUMVISIBLEENTITIES];
#endif

CVector CRenderer::ms_vecCameraPosition;
CVehicle *CRenderer::m_pFirstPersonVehicle;
bool CRenderer::m_loadingPriority;
#ifdef _3DS
/* Switch world geometry to its existing low-detail models earlier on 3DS.
 * Draw distance is unchanged; only the high-detail LOD range is scaled. */
float CRenderer::ms_lodDistScale = 0.65f;
#else
float CRenderer::ms_lodDistScale = 1.2f;
#endif

#ifdef EXTRA_MODEL_FLAGS
#define BACKFACE_CULLING_ON SetCullMode(rwCULLMODECULLBACK)
#define BACKFACE_CULLING_OFF SetCullMode(rwCULLMODECULLNONE)
#else
#define BACKFACE_CULLING_ON
#define BACKFACE_CULLING_OFF
#endif

// unused
BlockedRange CRenderer::aBlockedRanges[16];
BlockedRange *CRenderer::pFullBlockedRanges;
BlockedRange *CRenderer::pEmptyBlockedRanges;

#ifdef _3DS
#include "../../../../common/3ds/WorldDrawDistance.h"
static float gWorldLodScales[MODELINFOSIZE];

float
CRenderer::GetNew3DSWorldLodScale(CSimpleModelInfo *mi, int16 modelId, CEntity *ent)
{
	if(modelId < 0 || modelId >= MODELINFOSIZE) return 1.0f;
	float &scale = gWorldLodScales[modelId];
	if(scale == 0.0f) {
		const bool streetProp = IsStreetLight(modelId) || modelId == MI_TRAFFICLIGHTS ||
			modelId == MI_WASTEBIN || modelId == MI_BIN || modelId == MI_DUMP1 ||
			modelId == MI_POSTBOX1 || modelId == MI_NEWSSTAND ||
			modelId == MI_BUSSIGN1 || modelId == MI_NOPARKINGSIGN1 ||
			modelId == MI_PHONESIGN || modelId == MI_PHONEBOOTH1;
		scale = WorldDrawDistance3DS::Scale(mi->GetModelName(), IsTreeModel(modelId), streetProp);
	}
	float profileScale = scale;
	if(rw::c3d::stereoControlsActive() && !WorldDrawDistance3DS::IsIsland(mi->GetModelName())) {
		const bool streetProp = IsStreetLight(modelId) || modelId == MI_TRAFFICLIGHTS ||
			modelId == MI_WASTEBIN || modelId == MI_BIN || modelId == MI_DUMP1 ||
			modelId == MI_POSTBOX1 || modelId == MI_NEWSSTAND ||
			modelId == MI_BUSSIGN1 || modelId == MI_NOPARKINGSIGN1 ||
			modelId == MI_PHONESIGN || modelId == MI_PHONEBOOTH1;
		if(profileScale > 1.0f || IsTreeModel(modelId))
			profileScale = Min(profileScale, 0.75f);
		else if(streetProp)
			profileScale = Min(profileScale, 0.60f);
		else
			profileScale = Min(profileScale, 0.55f);
	}
	if(!rw::c3d::performanceModeActive() || WorldDrawDistance3DS::IsIsland(mi->GetModelName()))
		;
	else
		profileScale *= rw::c3d::stereoControlsActive() ? 1.0f : 0.65f;
	/* Stereo depth depends heavily on nearby silhouettes.  Keep small props in
	 * the forward hemisphere at a useful range and spend the reduced budget on
	 * objects behind the camera instead. */
	if(ent && !WorldDrawDistance3DS::IsIsland(mi->GetModelName())){
		const CVector offset = ent->GetBoundCentre() - ms_vecCameraPosition;
		const float distance = offset.Magnitude();
		const bool small = ent->GetBoundRadius() < 12.0f;
		const bool stereo = rw::c3d::stereoControlsActive();
		const float frontScale = stereo ? (small ? (rw::c3d::performanceModeActive() ? 0.70f : 0.85f) : 0.55f) : scale;
		profileScale = WorldDrawDistance3DS::WorldFrontScale(profileScale, Min(scale, frontScale),
			distance, DotProduct(offset, TheCamera.GetForward()), ent->GetBoundRadius(),
			stereo || rw::c3d::performanceModeActive());
	}
	if(!WorldDrawDistance3DS::IsIsland(mi->GetModelName()))
		profileScale *= CrowdDrawBudget3DS::DetailFactor(ent, mi->m_noFade);
	// sublightsb is a single 3000-unit shell for five Shoreside tower models
	// whose authored LOD range is 1000.  Give it the same effective far limit;
	// otherwise its huge bounds survive adaptive contraction after the towers.
	if(strcmp(mi->GetModelName(), "sublightsb") == 0)
		profileScale *= 1.0f/3.0f;
	return profileScale;
}

float
CRenderer::GetNew3DSWorldDistance(CEntity *ent, float originDistance, const CVector &cameraPosition)
{
	// Island LOD spheres cover whole islands: retain their authored origins.
	CSimpleModelInfo *mi = (CSimpleModelInfo*)CModelInfo::GetModelInfo(ent->GetModelIndex());
	const bool island = GetNew3DSWorldLodScale(mi, ent->GetModelIndex(), nil) == 1.0f;
	// The light shell spans all five project towers. Surface distance to that
	// giant combined sphere is not a useful proxy for any individual building.
	if(strcmp(mi->GetModelName(), "sublightsb") == 0)
		return originDistance;
	if(!ent->IsBuilding() || island || ent->GetBoundRadius() < 16.0f)
		return originDistance;
	return WorldDrawDistance3DS::SurfaceDistance(originDistance,
		(ent->GetBoundCentre() - cameraPosition).Magnitude(), ent->GetBoundRadius(), true, false);
}

namespace {
struct AttachedWindowLightModels
{
	int16 light;
	int16 carriers[5][2];
	uint8 numCarriers;
	bool requireAllCarriers;
};

static int16
FindModelId(const char *name)
{
	int id = -1;
	CModelInfo::GetModelInfo(name, &id);
	return id;
}

static bool
IsScheduledCarrier(CEntity *candidate, const CVector &lightPosition,
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
		// A carrier can remain queued with zero or near-zero final opacity while
		// its LOD transition is ending.  Do not let its full-bright window shell
		// survive that otherwise invisible frame.
		if(mi->m_alpha * fade < 16.0f)
			return false;
	}
	return true;
}
}

uint32
CRenderer::GetAttachedWindowLightMask(CEntity *ent)
{
	static bool modelsResolved;
	static AttachedWindowLightModels windows[3];
	if(!modelsResolved){
		windows[0].light = FindModelId("inwindows11");
		windows[0].carriers[0][0] = FindModelId("ind_mainten2");
		windows[0].carriers[0][1] = FindModelId("LOD_mainten2");
		windows[0].carriers[1][0] = FindModelId("iten_alleygun");
		windows[0].carriers[1][1] = FindModelId("LODn_alleygun");
		windows[0].carriers[2][0] = FindModelId("indbigbuild");
		windows[0].carriers[2][1] = FindModelId("LODbigbuild");
		windows[0].numCarriers = 3;
		windows[0].requireAllCarriers = true;

		windows[1].light = FindModelId("indwindows2");
		windows[1].carriers[0][0] = FindModelId("ind_newbuilds06");
		windows[1].carriers[0][1] = FindModelId("LOD_newbuilds06");
		windows[1].numCarriers = 1;
		windows[1].requireAllCarriers = true;

		// Shoreside Vale's 504 illuminated quads are stored in one giant
		// 3000-unit shell, while the five tower bodies use 290/1000-unit
		// detail/LOD pairs.  Keep the shell only while at least one of those
		// actual tower instances survived the final visibility pass.
		windows[2].light = FindModelId("sublightsb");
		windows[2].carriers[0][0] = FindModelId("towerflat26");
		windows[2].carriers[0][1] = FindModelId("LODerflat26");
		windows[2].carriers[1][0] = FindModelId("towerflat27");
		windows[2].carriers[1][1] = FindModelId("LODerflat27");
		windows[2].carriers[2][0] = FindModelId("towerflat28");
		windows[2].carriers[2][1] = FindModelId("LODerflat28");
		windows[2].carriers[3][0] = FindModelId("towerflat29");
		windows[2].carriers[3][1] = FindModelId("LODerflat29");
		windows[2].carriers[4][0] = FindModelId("towernew1");
		windows[2].carriers[4][1] = FindModelId("LODernew1");
		windows[2].numCarriers = 5;
		windows[2].requireAllCarriers = false;
		modelsResolved = true;
	}

	AttachedWindowLightModels *attachment = nil;
	for(uint32 i = 0; i < ARRAY_SIZE(windows); i++)
		if(ent->GetModelIndex() == windows[i].light){
			attachment = &windows[i];
			break;
		}
	if(attachment == nil)
		return 0xFFFFFFFF;

	// These night-time meshes contain only illuminated window polygons. Their
	// carrier buildings are separate IPL instances, so adaptive range can leave
	// the polygons floating unless the final render lists are checked together.
	const CVector lightPosition = ent->GetPosition();
	uint32 visibleMask = 0;
	for(uint32 carrier = 0; carrier < attachment->numCarriers; carrier++){
		const int16 detail = attachment->carriers[carrier][0];
		const int16 lod = attachment->carriers[carrier][1];
		bool found = false;
		for(int32 i = 0; i < ms_nNoOfVisibleEntities && !found; i++)
			found = IsScheduledCarrier(ms_aVisibleEntityPtrs[i], lightPosition, detail, lod);
#ifdef NEW_RENDERER
		for(int32 i = 0; i < ms_nNoOfVisibleBuildings && !found; i++)
			found = IsScheduledCarrier(ms_aVisibleBuildingPtrs[i], lightPosition, detail, lod);
		for(CLink<CVisibilityPlugins::AlphaObjectInfo> *node = CVisibilityPlugins::m_alphaBuildingList.head.next;
		    node != &CVisibilityPlugins::m_alphaBuildingList.tail && !found; node = node->next)
			found = IsScheduledCarrier(node->item.entity, lightPosition, detail, lod, node->item.sort);
#endif
		for(CLink<CVisibilityPlugins::AlphaObjectInfo> *node = CVisibilityPlugins::m_alphaEntityList.head.next;
		    node != &CVisibilityPlugins::m_alphaEntityList.tail && !found; node = node->next)
			found = IsScheduledCarrier(node->item.entity, lightPosition, detail, lod, node->item.sort);
		if(found)
			visibleMask |= 1u << carrier;
		if(attachment->requireAllCarriers && !found)
			return 0;
	}
	return attachment->requireAllCarriers ? 0xFFFFFFFF : visibleMask;
}
#endif

void
CRenderer::Init(void)
{
#ifdef _3DS
	memset(gWorldLodScales, 0, sizeof(gWorldLodScales));
#endif
	gSortedVehiclesAndPeds.Init(40);
	SortBIGBuildings();
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

#ifdef NEW_RENDERER
	if(gbNewRenderer){
		for(i = 0; i < ms_nNoOfVisibleVehicles; i++)
			ms_aVisibleVehiclePtrs[i]->PreRender();
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
	if(gbDontRenderBuildings)
		return;
	if(gbShowCollisionPolys)
		CCollision::DrawColModel_Coloured(e->GetMatrix(), *CModelInfo::GetModelInfo(e->GetModelIndex())->GetColModel(), e->GetModelIndex());
	else{
#ifdef EXTENDED_PIPELINES
		CustomPipes::AttachGlossPipe(e->GetAtomic());
#endif
#ifdef EXTRA_MODEL_FLAGS
	if(!e->IsBuilding() || CModelInfo::GetModelInfo(e->GetModelIndex())->RenderDoubleSided()){
		BACKFACE_CULLING_OFF;
		e->Render();
		BACKFACE_CULLING_ON;
	}else
#endif
		e->Render();
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
	const uint32 attachedWindowLightMask = GetAttachedWindowLightMask(e);
	if(attachedWindowLightMask == 0)
		return;
#endif

#ifndef MASTER
	if(gbShowCollisionPolys){
		if(!e->IsVehicle()){
			CCollision::DrawColModel_Coloured(e->GetMatrix(), *CModelInfo::GetModelInfo(e->GetModelIndex())->GetColModel(), e->GetModelIndex());
			return;
		}
	}else if(e->IsBuilding()){
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
#ifndef MASTER
		if(gbDontRenderPeds)
			return;
#endif
		ped = (CPed*)e;
		if(ped->m_nPedState == PED_DRIVING)
			return;
	}
#ifndef MASTER
	else if(e->IsObject() || e->IsDummy()){
		if(gbDontRenderObjects)
			return;
	}else if(e->IsVehicle()){
		// re3 addition
		if(gbDontRenderVehicles)
			return;
	}
#endif

#ifdef _3DS
	CrowdDrawBudget3DS::RenderScope crowdStyle(e);
	CameraOcclusion3DS::RenderScope cameraFade(e);
#endif
	resetLights = e->SetupLighting();

	if(e->IsVehicle())
		CVisibilityPlugins::InitAlphaAtomicList();

	// Render Peds in vehicle before vehicle itself
	if(e->IsVehicle()){
		veh = (CVehicle*)e;
#ifdef _3DS
		// Occupants use the same range as the vehicle's highest-detail geometry.
		// Once the body switches LOD, do not keep skinning invisible passengers.
		bool renderOccupants = CVisibilityPlugins::IsVehicleHighDetail((RpClump*)e->m_rwObject);
#else
		bool renderOccupants = true;
#endif
		if(renderOccupants){
		if(veh->pDriver && veh->pDriver->m_nPedState == PED_DRIVING)
			veh->pDriver->Render();
		for(i = 0; i < 8; i++)
			if(veh->pPassengers[i] && veh->pPassengers[i]->m_nPedState == PED_DRIVING)
				veh->pPassengers[i]->Render();
		}
		BACKFACE_CULLING_OFF;
	}
#ifdef EXTRA_MODEL_FLAGS
	if(!e->IsBuilding() || CModelInfo::GetModelInfo(e->GetModelIndex())->RenderDoubleSided()){
		BACKFACE_CULLING_OFF;
#ifdef _3DS
		rw::c3d::setWorldLightClusterMask(attachedWindowLightMask);
#endif
		e->Render();
#ifdef _3DS
		rw::c3d::setWorldLightClusterMask(0xFFFFFFFF);
#endif
		BACKFACE_CULLING_ON;
	}else
#endif
	{
#ifdef _3DS
		rw::c3d::setWorldLightClusterMask(attachedWindowLightMask);
#endif
	e->Render();
#ifdef _3DS
		rw::c3d::setWorldLightClusterMask(0xFFFFFFFF);
#endif
	}

	if(e->IsVehicle()){
		BACKFACE_CULLING_OFF;
		e->bImBeingRendered = true;
		CVisibilityPlugins::RenderAlphaAtomics();
		e->bImBeingRendered = false;
		BACKFACE_CULLING_ON;
	}

	e->RemoveLighting(resetLights);
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

inline bool IsRoad(CEntity *e) { return e->IsBuilding() && ((CBuilding*)e)->GetIsATreadable(); }


void
CRenderer::RenderRoads(void)
{
	int i;
	CTreadable *t;

	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	BACKFACE_CULLING_ON;
	DeActivateDirectional();
	SetAmbientColours();


	for(i = 0; i < ms_nNoOfVisibleEntities; i++){
		t = (CTreadable*)ms_aVisibleEntityPtrs[i];
		if(IsRoad(t)){
#ifndef MASTER
			if(gbShowCarRoadGroups || gbShowPedRoadGroups){
				int ind = 0;
				if(gbShowCarRoadGroups)
					ind += ThePaths.m_pathNodes[t->m_nodeIndices[PATH_CAR][0]].group;
				if(gbShowPedRoadGroups)
					ind += ThePaths.m_pathNodes[t->m_nodeIndices[PATH_PED][0]].group;
				SetAmbientColoursToIndicateRoadGroup(ind);
			}
#endif
			RenderOneRoad(t);
#ifndef MASTER
			if(gbShowCarRoadGroups || gbShowPedRoadGroups)
				ReSetAmbientAndDirectionalColours();
#endif
		}
	}
}

void
CRenderer::RenderEverythingBarRoads(void)
{
	int i;
	CEntity *e;
	CVector dist;
	EntityInfo ei;

	BACKFACE_CULLING_ON;
	gSortedVehiclesAndPeds.Clear();

	for(i = 0; i < ms_nNoOfVisibleEntities; i++){
		e = ms_aVisibleEntityPtrs[i];

		if(IsRoad(e))
			continue;

#ifdef EXTENDED_PIPELINES
		if(CustomPipes::bRenderingEnvMap && (e->IsPed() || e->IsVehicle()))
			continue;
#endif

		if(e->IsVehicle() ||
		   e->IsPed() && CVisibilityPlugins::GetClumpAlpha((RpClump*)e->m_rwObject) != 255){
			if(e->IsVehicle() && ((CVehicle*)e)->IsBoat()){
				ei.ent = e;
				dist = ms_vecCameraPosition - e->GetPosition();
				ei.sort = dist.MagnitudeSqr();
				gSortedVehiclesAndPeds.InsertSorted(ei);
			}else{
				dist = ms_vecCameraPosition - e->GetPosition();
				if(!CVisibilityPlugins::InsertEntityIntoSortedList(e, dist.Magnitude())){
					printf("Ran out of space in alpha entity list");
					RenderOneNonRoad(e);
				}
			}
		}else
			RenderOneNonRoad(e);
	}
}

void
CRenderer::RenderVehiclesButNotBoats(void)
{
	// This function doesn't do anything
	// because only boats are inserted into the list
	CLink<EntityInfo> *node;

	for(node = gSortedVehiclesAndPeds.tail.prev;
	    node != &gSortedVehiclesAndPeds.head;
	    node = node->prev){
		// only boats in this list
		CVehicle *v = (CVehicle*)node->item.ent;
		if(!v->IsBoat())
			RenderOneNonRoad(v);
	}
}

void
CRenderer::RenderBoats(void)
{
	CLink<EntityInfo> *node;

	BACKFACE_CULLING_ON;

	for(node = gSortedVehiclesAndPeds.tail.prev;
	    node != &gSortedVehiclesAndPeds.head;
	    node = node->prev){
		// only boats in this list
		CVehicle *v = (CVehicle*)node->item.ent;
		if(v->IsBoat())
			RenderOneNonRoad(v);
	}
}

#ifdef NEW_RENDERER
#ifndef LIBRW
#error "Need librw for EXTENDED_PIPELINES"
#endif
#include "WaterLevel.h"

enum {
	// blend passes
	PASS_NOZ,	// no z-write
	PASS_ADD,	// additive
	PASS_BLEND	// normal blend
};

static RwRGBAReal black;

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
	if(GetAttachedWindowLightMask(ent) == 0)
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
#ifdef _3DS
		camdist = GetNew3DSWorldDistance(ent, camdist) / GetNew3DSWorldLodScale(mi, ent->GetModelIndex(), ent);
#endif
		RpAtomic *lodatm;
		float fadefactor;
		uint32 alpha;

		lodatm = mi->GetAtomicFromDistance(camdist - FADE_DISTANCE);
		if(lodatm == nil) lodatm = atomic;
		fadefactor = (mi->GetLargestLodDistance() - (camdist - FADE_DISTANCE))/FADE_DISTANCE;
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

void
CRenderer::RenderWorld(int pass)
{
	int i;
	CEntity *e;
	CLink<CVisibilityPlugins::AlphaObjectInfo> *node;

	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	DeActivateDirectional();
	SetAmbientColours();

	// Temporary...have to figure out sorting better
	switch(pass){
	case 0:
		// Roads
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

		// KLUDGE for road puddles which have to be rendered at road-time
		// only very temporary, there are more rendering issues
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
		WorldRender::RenderBlendPass(PASS_BLEND);
		WorldRender::numBlendInsts[PASS_BLEND] = 0;
		break;
	case 1:
		// Opaque
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
		break;
	case 2:
		// Transparent
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
		WorldRender::RenderBlendPass(PASS_ADD);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
		WorldRender::RenderBlendPass(PASS_BLEND);
		break;
	}
}

void
CRenderer::RenderPeds(void)
{
	int i;
	CEntity *e;

	for(i = 0; i < ms_nNoOfVisibleVehicles; i++){
		e = ms_aVisibleVehiclePtrs[i];
		if(e->IsPed())
			RenderOneNonRoad(e);
	}
}

void
CRenderer::RenderVehicles(void)
{
	int i;
	CEntity *e;
	EntityInfo ei;
	CLink<EntityInfo> *node;

	// not the real thing
	for(i = 0; i < ms_nNoOfVisibleVehicles; i++){
		e = ms_aVisibleVehiclePtrs[i];
		if(!e->IsVehicle())
			continue;
//		if(PutIntoSortedVehicleList((CVehicle*)e))
//			continue;	// boats handled elsewhere
		ei.ent = e;
		ei.sort = (ms_vecCameraPosition - e->GetPosition()).MagnitudeSqr();
		gSortedVehiclesAndPeds.InsertSorted(ei);
	}

	for(node = gSortedVehiclesAndPeds.tail.prev;
	    node != &gSortedVehiclesAndPeds.head;
	    node = node->prev)
		RenderOneNonRoad(node->item.ent);
}

void
CRenderer::RenderWater(void)
{
	int i;
	CEntity *e;

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

	CWaterLevel::RenderWater();

	SetStencilState(0);
}

void
CRenderer::ClearForFrame(void)
{
	ms_nNoOfVisibleEntities = 0;
	ms_nNoOfVisibleVehicles = 0;
	ms_nNoOfVisibleBuildings = 0;
	ms_nNoOfInVisibleEntities = 0;
	gSortedVehiclesAndPeds.Clear();

	WorldRender::numBlendInsts[PASS_NOZ] = 0;
	WorldRender::numBlendInsts[PASS_ADD] = 0;
	WorldRender::numBlendInsts[PASS_BLEND] = 0;
}
#endif

void
CRenderer::RenderFadingInEntities(void)
{
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	BACKFACE_CULLING_ON;
	DeActivateDirectional();
	SetAmbientColours();
	CVisibilityPlugins::RenderFadingEntities();
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

// unused
void
CRenderer::RenderBlockBuildingLines(void)
{
	for(BlockedRange *br = pFullBlockedRanges; br; br = br->next)
		printf("Blocked: %f %f\n", br->a, br->b);
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
	if (mi->GetModelType() == MITYPE_TIME) {
 		ti = (CTimeModelInfo*)mi;
		other = ti->GetOtherTimeModel();
		if(CClock::GetIsTimeInRange(ti->GetTimeOn(), ti->GetTimeOff())){
			// don't fade in, or between time objects
			if(CANTIMECULL)
				ti->m_alpha = 255;
		}else{
			// Hide if possible
			if(CANTIMECULL)
				return VIS_INVISIBLE;
			// can't cull, so we'll try to draw this one, but don't request
			// it since what we really want is the other one.
			request = false;
		}
	}else{
		if (mi->GetModelType() != MITYPE_SIMPLE) {
			if(FindPlayerVehicle() == ent &&
#ifdef _3DS
			   !CameraOcclusion3DS::PresetVisible(ent) &&
#endif
			   TheCamera.Cams[TheCamera.ActiveCam].Mode == CCam::MODE_1STPERSON){
				// Player's vehicle in first person mode
				if(TheCamera.Cams[TheCamera.ActiveCam].DirectionWasLooking == LOOKING_FORWARD ||
				   ent->GetModelIndex() == MI_RHINO ||
				   ent->GetModelIndex() == MI_COACH ||
				   TheCamera.m_bInATunnelAndABigVehicle){
					ent->bNoBrightHeadLights = true;
				}else{
					m_pFirstPersonVehicle = (CVehicle*)ent;
					ent->bNoBrightHeadLights = false;
				}
				return VIS_OFFSCREEN;
			}
			// All sorts of Clumps
			if(ent->m_rwObject == nil || (!ent->bIsVisible
#ifdef _3DS
			   && !CameraOcclusion3DS::PresetVisible(ent)
#endif
			   ))
				return VIS_INVISIBLE;
			if(!ent->GetIsOnScreen())
				return VIS_OFFSCREEN;
			if(ent->bDrawLast){
				dist = (ent->GetPosition() - ms_vecCameraPosition).Magnitude();
				CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist);
				ent->bDistanceFade = false;
				return VIS_INVISIBLE;
			}
			return VIS_VISIBLE;
		}
		if(ent->IsObject() &&
		   ((CObject*)ent)->ObjectCreatedBy == TEMP_OBJECT){
			if(ent->m_rwObject == nil || (!ent->bIsVisible
#ifdef _3DS
			   && !CameraOcclusion3DS::PresetVisible(ent)
#endif
			   ))
				return VIS_INVISIBLE;
			return ent->GetIsOnScreen() ? VIS_VISIBLE : VIS_OFFSCREEN;
		}
	}

	// Simple ModelInfo

	dist = (ent->GetPosition() - ms_vecCameraPosition).Magnitude();
#ifdef _3DS
	float lodDist = GetNew3DSWorldDistance(ent, dist) / GetNew3DSWorldLodScale(mi, ent->GetModelIndex(), ent);
#else
	float lodDist = dist;
#endif

	// This can only happen with multi-atomic models (e.g. railtracks)
	// but why do we bump up the distance? can only be fading...
#ifndef _3DS
	if(LOD_DISTANCE + STREAM_DISTANCE < lodDist && lodDist < mi->GetLargestLodDistance())
		lodDist = mi->GetLargestLodDistance();
#endif

	if(ent->IsObject() && ent->bRenderDamaged)
		mi->m_isDamaged = true;

	RpAtomic *a = mi->GetAtomicFromDistance(lodDist);
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

		if(!ent->GetIsOnScreen()){
			mi->m_alpha = 255;
			return VIS_OFFSCREEN;
		}

		if(mi->m_alpha != 255){
			CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist);
			ent->bDistanceFade = true;
			return VIS_INVISIBLE;
		}

		if(mi->m_drawLast || ent->bDrawLast){
			CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist);
			ent->bDistanceFade = false;
			return VIS_INVISIBLE;
		}
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

	a = mi->GetAtomicFromDistance(lodDist - FADE_DISTANCE);
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

	if(!ent->GetIsOnScreen()){
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
	CSimpleModelInfo *mi = (CSimpleModelInfo *)CModelInfo::GetModelInfo(ent->GetModelIndex());
	CTimeModelInfo *ti;
	int32 other;

	if (mi->GetModelType() == MITYPE_TIME) {
 		ti = (CTimeModelInfo*)mi;
		other = ti->GetOtherTimeModel();
		// Hide objects not in time range if possible
		if(CANTIMECULL)
			if(!CClock::GetIsTimeInRange(ti->GetTimeOn(), ti->GetTimeOff()))
				return VIS_INVISIBLE;
		// Draw like normal
	} else if (mi->GetModelType() == MITYPE_VEHICLE)
		return ent->IsVisible() ? VIS_VISIBLE : VIS_INVISIBLE;

	float dist = (ms_vecCameraPosition-ent->GetPosition()).Magnitude();
#ifdef _3DS
	float lodDist = GetNew3DSWorldDistance(ent, dist) / GetNew3DSWorldLodScale(mi, ent->GetModelIndex(), ent);
#else
	float lodDist = dist;
#endif
	CSimpleModelInfo *nonLOD = mi->GetRelatedModel();

	// Find out whether to draw below near distance.
	// This is only the case if there is a non-LOD which is either not
	// loaded or not completely faded in yet.
	if(lodDist < mi->GetNearDistance() && lodDist < LOD_DISTANCE + STREAM_DISTANCE){
		// No non-LOD or non-LOD is completely visible.
		if(nonLOD == nil ||
		   nonLOD->GetRwObject() && nonLOD->m_alpha == 255)
			return VIS_INVISIBLE;

		// But if it is a time object, we'd rather draw the wrong
		// non-LOD than the right LOD.
		if (nonLOD->GetModelType() == MITYPE_TIME) {
			ti = (CTimeModelInfo*)nonLOD;
			other = ti->GetOtherTimeModel();
			if(other != -1 && CModelInfo::GetModelInfo(other)->GetRwObject())
				return VIS_INVISIBLE;
		}
	}

	RpAtomic *a = mi->GetAtomicFromDistance(lodDist);
	if(a){
		if(ent->m_rwObject == nil)
			ent->CreateRwObject();
		assert(ent->m_rwObject);
		RpAtomic *rwobj = (RpAtomic*)ent->m_rwObject;

		// Make sure our atomic uses the right geometry and not
		// that of an atomic for another draw distance.
		if(RpAtomicGetGeometry(a) != RpAtomicGetGeometry(rwobj))
			RpAtomicSetGeometry(rwobj, RpAtomicGetGeometry(a), rpATOMICSAMEBOUNDINGSPHERE); // originally 5 (mistake?)
		if (!ent->IsVisible() || !ent->GetIsOnScreenComplex())
			return VIS_INVISIBLE;
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
	a = mi->GetAtomicFromDistance(lodDist - FADE_DISTANCE);
	if(a == nil){
		ent->DeleteRwObject();
		return VIS_INVISIBLE;
	}

	// Fade...
	if(ent->m_rwObject == nil)
		ent->CreateRwObject();
	assert(ent->m_rwObject);
	RpAtomic *rwobj = (RpAtomic*)ent->m_rwObject;
	if(RpAtomicGetGeometry(a) != RpAtomicGetGeometry(rwobj))
		RpAtomicSetGeometry(rwobj, RpAtomicGetGeometry(a), rpATOMICSAMEBOUNDINGSPHERE); // originally 5 (mistake?)
	if (ent->IsVisible() && ent->GetIsOnScreenComplex()){
		ent->bDistanceFade = true;
		CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist);
	}
	return VIS_INVISIBLE;
}

void
CRenderer::ConstructRenderList(void)
{
#ifdef _3DS
	CrowdDrawBudget3DS::BeginFrame();
#endif
#ifdef NEW_RENDERER
	if(!gbNewRenderer)
#endif
{
	ms_nNoOfVisibleEntities = 0;
	ms_nNoOfInVisibleEntities = 0;
}
	ms_vecCameraPosition = TheCamera.GetPosition();

	// unused
	pFullBlockedRanges = nil;
	pEmptyBlockedRanges = aBlockedRanges;
	for(int i = 0; i < 16; i++){
		aBlockedRanges[i].prev = &aBlockedRanges[i-1];
		aBlockedRanges[i].next = &aBlockedRanges[i+1];
	}
	aBlockedRanges[0].prev = nil;
	aBlockedRanges[15].next = nil;

	// unused
	TestCloseThings = 0;
	TestBigThings = 0;

	ScanWorld();
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

#ifndef MASTER
	// missing in game but has to be done somewhere
	EntitiesRendered = 0;
	EntitiesNotRendered = 0;
	RenderedBigBuildings = 0;
	RenderedBuildings = 0;
	RenderedCars = 0;
	RenderedPeds = 0;
	RenderedObjects = 0;
	RenderedDummies = 0;
	TestedBigBuildings = 0;
	TestedBuildings = 0;
	TestedCars = 0;
	TestedPeds = 0;
	TestedObjects = 0;
	TestedDummies = 0;
#endif

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
		if(CCollision::ms_collisionInMemory!=LEVEL_GENERIC)ScanBigBuildingList(CWorld::GetBigBuildingList(CCollision::ms_collisionInMemory));
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
		CVehicle *train = FindPlayerTrain();
		if(train && train->GetPosition().z < 0.0f){
			poly[0].x = CWorld::GetSectorX(vectors[CORNER_CAM].x);
			poly[0].y = CWorld::GetSectorY(vectors[CORNER_CAM].y);
			poly[1].x = CWorld::GetSectorX(vectors[CORNER_LOD_LEFT].x);
			poly[1].y = CWorld::GetSectorY(vectors[CORNER_LOD_LEFT].y);
			poly[2].x = CWorld::GetSectorX(vectors[CORNER_LOD_RIGHT].x);
			poly[2].y = CWorld::GetSectorY(vectors[CORNER_LOD_RIGHT].y);
			ScanSectorPoly(poly, 3, ScanSectorList_Subway);
		}else{
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
#ifdef NO_ISLAND_LOADING
			if (CMenuManager::m_PrefsIslandLoading == CMenuManager::ISLAND_LOADING_HIGH) {
				ScanBigBuildingList(CWorld::GetBigBuildingList(LEVEL_INDUSTRIAL));
				ScanBigBuildingList(CWorld::GetBigBuildingList(LEVEL_COMMERCIAL));
				ScanBigBuildingList(CWorld::GetBigBuildingList(LEVEL_SUBURBAN));
			} else 
#endif
			{
	#ifdef FIX_BUGS
			if (CCollision::ms_collisionInMemory != LEVEL_GENERIC)
	#endif
				ScanBigBuildingList(CWorld::GetBigBuildingList(CCollision::ms_collisionInMemory));
			}
			ScanBigBuildingList(CWorld::GetBigBuildingList(LEVEL_GENERIC));
		}
	}

#ifndef MASTER
	if(gbShowCullZoneDebugStuff){
		sprintf(gString, "Rejected: %d/%d.", EntitiesNotRendered, EntitiesNotRendered + EntitiesRendered);
		CDebug::PrintAt(gString, 10, 10);
		sprintf(gString, "Tested:BBuild:%d Build:%d Peds:%d Cars:%d Obj:%d Dummies:%d",
			TestedBigBuildings, TestedBuildings, TestedPeds, TestedCars, TestedObjects, TestedDummies);
		CDebug::PrintAt(gString, 10, 11);
		sprintf(gString, "Rendered:BBuild:%d Build:%d Peds:%d Cars:%d Obj:%d Dummies:%d",
			RenderedBigBuildings, RenderedBuildings, RenderedPeds, RenderedCars, RenderedObjects, RenderedDummies);
		CDebug::PrintAt(gString, 10, 12);
	}
#endif
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
	DeActivateDirectional();
	SetAmbientColours();
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
		if (!bHasBlip && lightMult != 1.0f) {
			SetAmbientAndDirectionalColours(lightMult);
			return true;
		}
	}
	return false;
}

void
CPed::RemoveLighting(bool reset)
{
	CRenderer::RemoveVehiclePedLights(this, reset);
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

void
CRenderer::InsertEntityIntoList(CEntity *ent)
{
#ifdef FIX_BUGS
	if (!ent->m_rwObject) return;
#endif

#ifdef NEW_RENDERER
	// TODO: there are more flags being checked here
	if(gbNewRenderer && (ent->IsVehicle() || ent->IsPed()))
		ms_aVisibleVehiclePtrs[ms_nNoOfVisibleVehicles++] = ent;
	else if(gbNewRenderer && ent->IsBuilding())
		ms_aVisibleBuildingPtrs[ms_nNoOfVisibleBuildings++] = ent;
	else
#endif
		ms_aVisibleEntityPtrs[ms_nNoOfVisibleEntities++] = ent;
}

void
CRenderer::ScanBigBuildingList(CPtrList &list)
{
	CPtrNode *node;
	CEntity *ent;

	for(node = list.first; node; node = node->next){
		ent = (CEntity*)node->item;
#ifndef MASTER
		// all missing from game actually
		TestedBigBuildings++;
#endif
		if(!ent->bZoneCulled){
			if(SetupBigBuildingVisibility(ent) == VIS_VISIBLE)
				InsertEntityIntoList(ent);
#ifndef MASTER
			EntitiesRendered++;
			RenderedBigBuildings++;
		}else{
			EntitiesNotRendered++;
#endif
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

	for(i = 0; i < NUMSECTORENTITYLISTS; i++){
		list = &lists[i];
		for(node = list->first; node; node = node->next){
			ent = (CEntity*)node->item;
			if(ent->m_scanCode == CWorld::GetCurrentScanCode())
				continue;	// already seen
			ent->m_scanCode = CWorld::GetCurrentScanCode();

			if(IsEntityCullZoneVisible(ent)){
				switch(SetupEntityVisibility(ent)){
				case VIS_VISIBLE:
					InsertEntityIntoList(ent);
					break;
				case VIS_INVISIBLE:
					if(!IsGlass(ent->GetModelIndex()))
						break;
					// fall through
				case VIS_OFFSCREEN:
					dx = ms_vecCameraPosition.x - ent->GetPosition().x;
					dy = ms_vecCameraPosition.y - ent->GetPosition().y;
					if(dx > -65.0f && dx < 65.0f &&
					   dy > -65.0f && dy < 65.0f &&
					   ms_nNoOfInVisibleEntities < NUMINVISIBLEENTITIES - 1)
						ms_aInVisibleEntityPtrs[ms_nNoOfInVisibleEntities++] = ent;
					break;
				case VIS_STREAMME:
					if(!CStreaming::ms_disableStreaming)
						if(!m_loadingPriority || CStreaming::ms_numModelsRequested < 10)
							CStreaming::RequestModel(ent->GetModelIndex(), 0);
					break;
				}
#ifndef MASTER
				EntitiesRendered++;
				switch(ent->GetType()){
				case ENTITY_TYPE_BUILDING:
					if(ent->bIsBIGBuilding)
						RenderedBigBuildings++;
					else
						RenderedBuildings++;
					break;
				case ENTITY_TYPE_VEHICLE:
					RenderedCars++;
					break;
				case ENTITY_TYPE_PED:
					RenderedPeds++;
					break;
				case ENTITY_TYPE_OBJECT:
					RenderedObjects++;
					break;
				case ENTITY_TYPE_DUMMY:
					RenderedDummies++;
					break;
				}
#endif
			}else if(IsRoad(ent) && !CStreaming::ms_disableStreaming){
				if(SetupEntityVisibility(ent) == VIS_STREAMME)
					if(!m_loadingPriority || CStreaming::ms_numModelsRequested < 10)
						CStreaming::RequestModel(ent->GetModelIndex(), 0);
			}else{
#ifndef MASTER
				EntitiesNotRendered++;
#endif
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

	for(i = 0; i < NUMSECTORENTITYLISTS; i++){
		list = &lists[i];
		for(node = list->first; node; node = node->next){
			ent = (CEntity*)node->item;
			if(ent->m_scanCode == CWorld::GetCurrentScanCode())
				continue;	// already seen
			ent->m_scanCode = CWorld::GetCurrentScanCode();

			if(IsEntityCullZoneVisible(ent)){
				switch(SetupEntityVisibility(ent)){
				case VIS_VISIBLE:
					InsertEntityIntoList(ent);
					break;
				case VIS_INVISIBLE:
					if(!IsGlass(ent->GetModelIndex()))
						break;
					// fall through
				case VIS_OFFSCREEN:
					dx = ms_vecCameraPosition.x - ent->GetPosition().x;
					dy = ms_vecCameraPosition.y - ent->GetPosition().y;
					if(dx > -65.0f && dx < 65.0f &&
					   dy > -65.0f && dy < 65.0f &&
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
#ifndef MASTER
				// actually missing in game
				EntitiesRendered++;
				switch(ent->GetType()){
				case ENTITY_TYPE_BUILDING:
					if(ent->bIsBIGBuilding)
						RenderedBigBuildings++;
					else
						RenderedBuildings++;
					break;
				case ENTITY_TYPE_VEHICLE:
					RenderedCars++;
					break;
				case ENTITY_TYPE_PED:
					RenderedPeds++;
					break;
				case ENTITY_TYPE_OBJECT:
					RenderedObjects++;
					break;
				case ENTITY_TYPE_DUMMY:
					RenderedDummies++;
					break;
				}
#endif
			}else if(IsRoad(ent) && !CStreaming::ms_disableStreaming){
				if(SetupEntityVisibility(ent) == VIS_STREAMME)
					CStreaming::RequestModel(ent->GetModelIndex(), 0);
			}else{
#ifndef MASTER
				// actually missing in game
				EntitiesNotRendered++;
#endif
			}
		}
	}
}

void
CRenderer::ScanSectorList_Subway(CPtrList *lists)
{
	CPtrNode *node;
	CPtrList *list;
	CEntity *ent;
	int i;
	float dx, dy;

	for(i = 0; i < NUMSECTORENTITYLISTS; i++){
		list = &lists[i];
		for(node = list->first; node; node = node->next){
			ent = (CEntity*)node->item;
			if(ent->m_scanCode == CWorld::GetCurrentScanCode())
				continue;	// already seen
			ent->m_scanCode = CWorld::GetCurrentScanCode();
			switch(SetupEntityVisibility(ent)){
			case VIS_VISIBLE:
				InsertEntityIntoList(ent);
				break;
			case VIS_OFFSCREEN:
				dx = ms_vecCameraPosition.x - ent->GetPosition().x;
				dy = ms_vecCameraPosition.y - ent->GetPosition().y;
				if(dx > -65.0f && dx < 65.0f &&
				   dy > -65.0f && dy < 65.0f &&
				   ms_nNoOfInVisibleEntities < NUMINVISIBLEENTITIES - 1)
					ms_aInVisibleEntityPtrs[ms_nNoOfInVisibleEntities++] = ent;
				break;
			}
		}
	}
}

void
CRenderer::ScanSectorList_RequestModels(CPtrList *lists)
{
	CPtrNode *node;
	CPtrList *list;
	CEntity *ent;
	int i;

	for(i = 0; i < NUMSECTORENTITYLISTS; i++){
		list = &lists[i];
		for(node = list->first; node; node = node->next){
			ent = (CEntity*)node->item;
			if(ent->m_scanCode == CWorld::GetCurrentScanCode())
				continue;	// already seen
			ent->m_scanCode = CWorld::GetCurrentScanCode();
			if(IsEntityCullZoneVisible(ent))
			if(ShouldModelBeStreamed(ent))
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
CRenderer::ShouldModelBeStreamed(CEntity *ent)
{
	CSimpleModelInfo *mi = (CSimpleModelInfo *)CModelInfo::GetModelInfo(ent->GetModelIndex());
	float dist = (ent->GetPosition() - ms_vecCameraPosition).Magnitude();
#ifdef _3DS
	dist = GetNew3DSWorldDistance(ent, dist, ms_vecCameraPosition) / GetNew3DSWorldLodScale(mi, ent->GetModelIndex(), ent);
#endif
	if(mi->m_noFade)
		return dist - STREAM_DISTANCE < mi->GetLargestLodDistance();
	else
		return dist - FADE_DISTANCE - STREAM_DISTANCE < mi->GetLargestLodDistance();
}

bool
CRenderer::IsEntityCullZoneVisible(CEntity *ent)
{
	CPed *ped;
	CObject *obj;

	if(gbDisableZoneCull) return true;

#ifndef MASTER
	switch(ent->GetType()){
	case ENTITY_TYPE_BUILDING:
		if(ent->bIsBIGBuilding)
			TestedBigBuildings++;
		else
			TestedBuildings++;
		break;
	case ENTITY_TYPE_VEHICLE:
		TestedCars++;
		break;
	case ENTITY_TYPE_PED:
		TestedPeds++;
		break;
	case ENTITY_TYPE_OBJECT:
		TestedObjects++;
		break;
	case ENTITY_TYPE_DUMMY:
		TestedDummies++;
		break;
	}
#endif
	if(ent->bZoneCulled)
		return false;


	switch(ent->GetType()){
	case ENTITY_TYPE_VEHICLE:
		return IsVehicleCullZoneVisible(ent);
	case ENTITY_TYPE_PED:
		ped = (CPed*)ent;
		if (ped->bInVehicle) {
			if (ped->m_pMyVehicle)
				return IsVehicleCullZoneVisible(ped->m_pMyVehicle);
			else
				return true;
		}
		return !(ped->m_pCurSurface && ped->m_pCurSurface->bZoneCulled2);
	case ENTITY_TYPE_OBJECT:
		obj = (CObject*)ent;
		if(!obj->GetIsStatic())
			return true;
		return !(obj->m_pCurSurface && obj->m_pCurSurface->bZoneCulled2);
	default: break;
	}
	return true;
}

bool
CRenderer::IsVehicleCullZoneVisible(CEntity *ent)
{
	CVehicle *v = (CVehicle*)ent;
	switch(v->GetStatus()) {
	case STATUS_SIMPLE:
	case STATUS_PHYSICS:
	case STATUS_ABANDONED:
	case STATUS_WRECKED:
		return !(v->m_pCurGroundEntity && v->m_pCurGroundEntity->bZoneCulled2);
	default: break;
	}
	return true;
}

void
CRenderer::RemoveVehiclePedLights(CEntity *ent, bool reset)
{
	if(ent->bRenderScorched){
		WorldReplaceScorchedLightsWithNormal(Scene.world);
		return;
	}
	CPointLights::RemoveLightsAffectingObject();
	if(reset)
		ReSetAmbientAndDirectionalColours();
}
