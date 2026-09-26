#pragma once

#ifdef _3DS
#include "Camera.h"
#include "CutsceneMgr.h"
#include "PlayerPed.h"
#include "Pools.h"
#include "Script.h"
#include "Timer.h"
#include "World.h"
#include "main.h"

namespace CrowdDrawBudget3DS
{
struct Entry {
	CEntity *entity;
	float score;
};
static Entry selected[2][32];
static int counts[2];
static CEntity *previous[2][32];
static int previousCounts[2];
static bool enabled;
static int mode;
static float worldScale = 1.0f;
static float detailScale = 1.0f;
static uint32 lastTime;
static float protectedRange[2], outerRange[2];
struct FadeState {
	CEntity *entity;
	int handle;
	float opacity, reflection;
};
static FadeState fadeStates[2][512];
#ifdef RESTORIES_3DS_BUILD
static int nearbyCars;
static CVehicle *reflectionCar;
#else
static CVehicle *reflectionCars[4];
static int reflectionCarCount;
#endif

inline bool
HideTarget(CEntity *entity);
inline bool
Protected(CEntity *entity);

inline FadeState *
State(CEntity *entity)
{
	if(!entity || (!entity->IsVehicle() && !entity->IsPed())) return nil;
	const int kind = entity->IsVehicle() ? 0 : 1;
	const int handle = kind == 0 ? CPools::GetVehiclePool()->GetIndex((CVehicle *)entity) : CPools::GetPedPool()->GetIndex((CPed *)entity);
	const int slot = handle >> 8;
	if(slot < 0 || slot >= 512) return nil;
	FadeState &state = fadeStates[kind][slot];
	if(state.entity != entity || state.handle != handle) {
		state.entity = entity;
		state.handle = handle;
		state.opacity = 1.f;
#ifdef RESTORIES_3DS_BUILD
		state.reflection = 1.f;
#else
		CPlayerPed *player = FindPlayerPed();
		state.reflection = !entity->IsVehicle() ? 1.f :
			(player && entity == player->m_pMyVehicle ? 0.4f : 0.f);
#endif
	}
	return &state;
}

inline float
Approach(float value, float target, float step)
{
	return value < target ? Min(value + step, target) : Max(value - step, target);
}

inline void
UpdateFade(CEntity *entity, float dt)
{
	FadeState *state = State(entity);
	if(!state) return;
	const int kind = entity->IsVehicle() ? 0 : 1;
	float target = 1.f;
	if(enabled && !Protected(entity)) {
		const float distance = (entity->GetPosition() - TheCamera.GetPosition()).Magnitude();
		const float end = HideTarget(entity) ? protectedRange[kind] + 10.f : outerRange[kind];
		target = Max(0.f, Min(1.f, (end - distance) / 10.f));
	}
	state->opacity = Approach(state->opacity, target, dt / 0.35f);
	if(enabled && !Protected(entity)) {
		const float distance = (entity->GetPosition() - TheCamera.GetPosition()).Magnitude();
		// Finish before the engine's own far clip; temporal smoothing must not
		// carry a partly visible car across that hard boundary at high speed.
		state->opacity = Min(state->opacity, Max(0.f, Min(1.f, (outerRange[kind] - distance) / 10.f)));
	}
	float reflection = 1.f;
	CPlayerPed *player = FindPlayerPed();
#ifdef RESTORIES_3DS_BUILD
	if(entity->IsVehicle() && nearbyCars >= 3 && !CCutsceneMgr::IsRunning() && entity != reflectionCar && (!player || entity != player->m_pMyVehicle)) {
		CVehicle *vehicle = (CVehicle *)entity;
		if(!vehicle->IsBoat() && !vehicle->IsPlane() && !vehicle->IsHeli() && !vehicle->IsTrain()) reflection = 0.f;
	}
#else
	if(entity->IsVehicle()) {
		// GTA3/VC use the same bright Neo environment map. Keep the player's
		// vehicle and the selected traffic reflections, but at 40% strength.
		reflection = 0.4f;
		CVehicle *vehicle = (CVehicle *)entity;
		const bool trafficCar = (!player || vehicle != player->m_pMyVehicle) &&
			!vehicle->IsBoat() && !vehicle->IsPlane() && !vehicle->IsHeli() && !vehicle->IsTrain();
		if(trafficCar && !CCutsceneMgr::IsRunning()) {
			bool selectedForReflection = false;
			for(int i = 0; i < reflectionCarCount; ++i)
				if(reflectionCars[i] == vehicle) {
					selectedForReflection = true;
					break;
				}
			if(!selectedForReflection) reflection = 0.f;
		}
	}
#endif
	state->reflection = Approach(state->reflection, reflection, dt / 0.35f);
}

inline bool
Essential(CEntity *entity)
{
	if(!entity) return true;
	if((entity->GetPosition() - TheCamera.GetPosition()).MagnitudeSqr() < 18.0f * 18.0f) return true;
	CPlayerPed *player = FindPlayerPed();
	if(player && ((entity->GetPosition() - player->GetPosition()).MagnitudeSqr() < 18.0f * 18.0f || entity == player || entity == player->m_pMyVehicle))
		return true;
	if(entity->IsVehicle()) {
		CVehicle *vehicle = (CVehicle *)entity;
		return vehicle->VehicleCreatedBy != RANDOM_VEHICLE || vehicle->IsBoat() || vehicle->IsPlane() || vehicle->IsHeli() || vehicle->IsTrain();
	}
	if(entity->IsPed()) return ((CPed *)entity)->CharCreatedBy != RANDOM_CHAR || ((CPed *)entity)->IsPlayer() || ((CPed *)entity)->bInVehicle;
	return true;
}

inline bool
Protected(CEntity *entity)
{
	if(Essential(entity)) return true;
	const int kind = entity->IsVehicle() ? 0 : 1;
	return (entity->GetPosition() - TheCamera.GetPosition()).MagnitudeSqr() <= protectedRange[kind] * protectedRange[kind];
}

inline void
Consider(CEntity *entity, int kind, int budget, float range)
{
	if(!entity || !entity->bIsVisible || !entity->GetIsOnScreen()) return;
	float score = (entity->GetPosition() - TheCamera.GetPosition()).MagnitudeSqr();
	if(score > range * range) return;
	for(int i = 0; i < previousCounts[kind]; ++i)
		if(previous[kind][i] == entity) {
			score *= 0.80f;
			break;
		}
	int pos = 0;
	while(pos < counts[kind] && selected[kind][pos].score <= score) ++pos;
	if(pos >= budget) return;
	int end = counts[kind] < budget ? counts[kind]++ : budget - 1;
	for(int i = end; i > pos; --i) selected[kind][i] = selected[kind][i - 1];
	selected[kind][pos] = {entity, score};
}

inline void
BeginFrame()
{
	const bool stereo = rw::c3d::stereoControlsActive();
	mode = (stereo ? 2 : 0) + (rw::c3d::performanceModeActive() ? 1 : 0);
	// During missions/races/cutscenes prefer false negatives over hiding actors.
	enabled = !CTheScripts::IsPlayerOnAMission() && !CCutsceneMgr::IsRunning();
	const int cars[4] = {10, 7, 6, 4};
	const int peds[4] = {16, 12, 10, 8};
	const float vehicleDetail[4] = {1.f, 0.80f, 0.90f, 0.75f};
	const float high = 70.f * TheCamera.GenerationDistMultiplier * 0.60f * vehicleDetail[mode];
	const float lowEnd = 90.f * TheCamera.GenerationDistMultiplier;
	// Keep most of the low-detail band before crowd pressure may hide a car.
	protectedRange[0] = high + Max(20.f, (lowEnd - high) * 0.60f);
	outerRange[0] = Max(lowEnd, protectedRange[0] + 10.f);
	const float pedRange[4] = {55.f, 45.f, 42.f, 35.f};
	protectedRange[1] = Max(30.f, pedRange[mode] * TheCamera.LODDistMultiplier);
	outerRange[1] = protectedRange[1] + 15.f;
	for(int k = 0; k < 2; ++k) {
		previousCounts[k] = counts[k];
		for(int i = 0; i < counts[k]; ++i) previous[k][i] = selected[k][i].entity;
		counts[k] = 0;
	}
	if(enabled && CPools::GetVehiclePool() && CPools::GetPedPool()) {
		for(int i = 0; i < CPools::GetVehiclePool()->GetSize(); ++i) Consider(CPools::GetVehiclePool()->GetSlot(i), 0, cars[mode], outerRange[0]);
		for(int i = 0; i < CPools::GetPedPool()->GetSize(); ++i) Consider(CPools::GetPedPool()->GetSlot(i), 1, peds[mode], outerRange[1]);
	}
	uint32 now = CTimer::GetTimeInMillisecondsPauseMode();
	float dt = lastTime && now >= lastTime ? (now - lastTime) * 0.001f : 0.f;
	lastTime = now;
	if(dt > 0.1f) dt = 0.1f;
	CPlayerPed *player = FindPlayerPed();
#ifdef RESTORIES_3DS_BUILD
	nearbyCars = 0;
	CVehicle *nearest = nil;
	float nearestScore = 1.0e30f;
	if(CPools::GetVehiclePool()) {
		for(int i = 0; i < CPools::GetVehiclePool()->GetSize(); ++i) {
			CVehicle *vehicle = CPools::GetVehiclePool()->GetSlot(i);
			if(!vehicle) {
				if(i < 512) fadeStates[0][i].entity = nil;
				continue;
			}
			if(!vehicle->bIsVisible || !vehicle->GetIsOnScreen()) continue;
			float score = (vehicle->GetPosition() - TheCamera.GetPosition()).MagnitudeSqr();
			if(score > 60.f * 60.f) continue;
			++nearbyCars;
			if(player && vehicle == player->m_pMyVehicle) continue;
			if(vehicle == reflectionCar) score *= 0.80f;
			if(score < nearestScore) {
				nearestScore = score;
				nearest = vehicle;
			}
		}
		reflectionCar = nearest;
		for(int i = 0; i < CPools::GetVehiclePool()->GetSize(); ++i) UpdateFade(CPools::GetVehiclePool()->GetSlot(i), dt);
	}
#else
	reflectionCarCount = 0;
	const int reflectionBudget = stereo ? 2 : 4;
	float reflectionScores[4] = {1.0e30f, 1.0e30f, 1.0e30f, 1.0e30f};
	if(CPools::GetVehiclePool()) {
		for(int i = 0; i < CPools::GetVehiclePool()->GetSize(); ++i) {
			CVehicle *vehicle = CPools::GetVehiclePool()->GetSlot(i);
			if(!vehicle) {
				if(i < 512) fadeStates[0][i].entity = nil;
				continue;
			}
			if((player && vehicle == player->m_pMyVehicle) || !vehicle->bIsVisible || !vehicle->GetIsOnScreen() ||
			   vehicle->IsBoat() || vehicle->IsPlane() || vehicle->IsHeli() || vehicle->IsTrain())
				continue;
			const float score = (vehicle->GetPosition() - TheCamera.GetPosition()).MagnitudeSqr();
			if(score > 60.f * 60.f) continue;
			int pos = 0;
			while(pos < reflectionCarCount && reflectionScores[pos] <= score) ++pos;
			if(pos >= reflectionBudget) continue;
			const int end = reflectionCarCount < reflectionBudget ? reflectionCarCount++ : reflectionBudget - 1;
			for(int j = end; j > pos; --j) {
				reflectionCars[j] = reflectionCars[j - 1];
				reflectionScores[j] = reflectionScores[j - 1];
			}
			reflectionCars[pos] = vehicle;
			reflectionScores[pos] = score;
		}
		for(int i = 0; i < CPools::GetVehiclePool()->GetSize(); ++i) UpdateFade(CPools::GetVehiclePool()->GetSlot(i), dt);
	}
#endif
	if(CPools::GetPedPool()) {
		for(int i = 0; i < CPools::GetPedPool()->GetSize(); ++i) {
			CPed *ped = CPools::GetPedPool()->GetSlot(i);
			if(!ped) {
				if(i < 512) fadeStates[1][i].entity = nil;
				continue;
			}
			UpdateFade(ped, dt);
		}
	}
	// Use the same missed-frame signal in Flat and Stereo. Reduce the whole
	// ordinary-world range first; only begin shortening detailed-building LODs
	// after that first stage has settled and frame pressure remains high.
	static int previousStereo = -1;
	const bool returnedToFlat = previousStereo == 1 && !stereo;
	previousStereo = stereo ? 1 : 0;
	if(returnedToFlat) {
		// Stereo pressure is stale as soon as one-eye rendering resumes. Restore
		// the full world immediately; Flat will contract again only if its own
		// missed presentations prove that the scene is still too expensive.
		worldScale = detailScale = 1.f;
		rw::c3d::resetFrameSkipPressure();
	}
	const float pressure = returnedToFlat ? 0.f : Min(1.f, rw::c3d::frameSkipPressure() * 1.5f);
	const float worldFloor[4] = {0.82f, 0.70f, 0.64f, 0.52f};
	const float worldTarget = 1.f - (1.f - worldFloor[mode]) * pressure;
	if(worldScale > worldTarget)
		worldScale = Max(worldTarget, worldScale - dt * (0.22f + 0.50f * pressure));
	else
		worldScale = Min(worldTarget, worldScale + dt * 0.20f);
	rw::c3d::setAdaptiveWorldRangeScale(worldScale);

	float detailPressure = 0.f;
	if(pressure > 0.65f && worldScale <= worldTarget + 0.02f) detailPressure = Min(1.f, (pressure - 0.65f) / 0.35f);
	const float detailFloor[4] = {0.88f, 0.76f, 0.72f, 0.55f};
	const float detailTarget = 1.f - (1.f - detailFloor[mode]) * detailPressure;
	if(detailScale > detailTarget)
		detailScale = Max(detailTarget, detailScale - dt * (0.12f + 0.35f * detailPressure));
	else
		detailScale = Min(detailTarget, detailScale + dt * 0.16f);
}

inline bool
HideTarget(CEntity *entity)
{
	if(!enabled || Protected(entity)) return false;
	int kind = entity->IsVehicle() ? 0 : 1;
	for(int i = 0; i < counts[kind]; ++i)
		if(selected[kind][i].entity == entity) return false;
	return true;
}

inline bool
Hide(CEntity *entity)
{
	FadeState *state = State(entity);
	return state && state->opacity <= 0.f;
}

struct RenderScope {
	rw::c3d::EntityRenderStyle previous;
	explicit RenderScope(CEntity *entity, bool untexturedBlackDecal = false) : previous(rw::c3d::getEntityRenderStyle())
	{
		FadeState *state = State(entity);
		rw::c3d::EntityRenderStyle style = {state ? state->opacity : 1.f, state ? state->reflection : 1.f, untexturedBlackDecal};
		rw::c3d::setEntityRenderStyle(style);
	}
	~RenderScope() { rw::c3d::setEntityRenderStyle(previous); }
};

inline float
DetailDistanceFactor(float surfaceDistance)
{
	float farWeight = (surfaceDistance - 60.f) / 60.f;
	if(farWeight < 0.f) farWeight = 0.f;
	if(farWeight > 1.f) farWeight = 1.f;
	return 1.f - (1.f - detailScale) * farWeight;
}

inline float
DetailFactor(CEntity *entity, bool noFade)
{
	// Keep island/large fallback LODs stable; only the detailed background contracts.
	if(detailScale == 1.f || !entity || !entity->IsBuilding() || entity->bIsBIGBuilding || noFade) return 1.f;
	const float radius = entity->GetBoundRadius();
	if(radius < 12.f) return 1.f;
	float distance = (entity->GetBoundCentre() - TheCamera.GetPosition()).Magnitude() - radius;
	return DetailDistanceFactor(distance);
}
} // namespace CrowdDrawBudget3DS
#endif
