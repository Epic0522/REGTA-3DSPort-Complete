#pragma once
#ifdef _3DS
#include "CameraOcclusion.h"
#include "ModelInfo.h"
namespace CameraCollision3DS
{
struct QueryState {
	bool active, ignoreActors;
	CEntity *vehicle;
};
inline QueryState &
State()
{
	static QueryState state = {false, false, 0};
	return state;
}
struct Scope {
	QueryState previous;
	Scope(bool active, bool actors, CEntity *vehicle) : previous(State()) { State() = {active, actors, vehicle}; }
	~Scope() { State() = previous; }
};
inline bool
Prefix(const char *text, const char *prefix)
{
	while(*prefix) {
		char c = *text++;
		if(c >= 'A' && c <= 'Z') c += 'a' - 'A';
		if(c != *prefix++) return false;
	}
	return true;
}
inline bool
ThinProp(const char *name)
{
	return Prefix(name, "lamppost") || Prefix(name, "mlamppost") || Prefix(name, "streetlamp") || Prefix(name, "streetlight") ||
	       Prefix(name, "doublestreetl") || Prefix(name, "trafficlight") || Prefix(name, "traffic_light") || Prefix(name, "mtraffic") ||
	       Prefix(name, "tlight_post") || Prefix(name, "tlight_walk") || Prefix(name, "telpole") || Prefix(name, "telgrphpole") ||
	       Prefix(name, "bollardlight");
}
inline bool
Ignore(CEntity *entity)
{
	const QueryState &state = State();
	if(!state.active || !entity) return false;
	if(state.ignoreActors && (entity->IsPed() || entity == state.vehicle)) return true;
	if(entity->IsPed() || entity->IsVehicle() || entity->GetModelIndex() < 0) return false;
	// Thin props now use the existing camera collision tests too; their alpha
	// hides the obstruction while the comfort filter moves the camera inward.
	return false;
}
} // namespace CameraCollision3DS
#endif
