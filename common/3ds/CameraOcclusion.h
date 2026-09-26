#pragma once
#ifdef _3DS
// A small shared registry. No allocation, model edits, or extra world queries.
namespace CameraOcclusion3DS
{
struct Entry {
	CEntity *entity;
	float alpha;
	bool touched;
};
struct Registry {
	Entry entries[32] = {};
	int count = 0;
	bool collect = false, overflow = false, contact = false;
};
// Per-frame preset overrides, separate from the fading contact registry.
struct PresetVisibility {
	CEntity *vehicle = nullptr;
	CEntity *occupants[9] = {};
	int count = 0;
	bool hoodSettled = false;
	float opacity = .4f;
};
inline PresetVisibility &
Preset()
{
	static PresetVisibility value;
	return value;
}
inline bool
PresetVisible(CEntity *entity)
{
	auto &p = Preset();
	if(!entity || p.hoodSettled) return false;
	if(entity == p.vehicle) return true;
	for(int i = 0; i < p.count; ++i)
		if(entity == p.occupants[i]) return true;
	return false;
}
inline bool
HideFixedShadow(CEntity *entity)
{
	return entity && Preset().hoodSettled && entity == Preset().vehicle;
}
inline Registry &
Data()
{
	static Registry data;
	return data;
}
inline void
Forget(CEntity *entity)
{
	auto &p = Preset();
	if(p.vehicle == entity) p = PresetVisibility{};
	for(int i = 0; i < p.count; ++i)
		if(p.occupants[i] == entity) p.occupants[i] = nullptr;
	auto &d = Data();
	for(int i = 0; i < d.count; ++i)
		if(d.entries[i].entity == entity) {
			d.entries[i] = d.entries[--d.count];
			return;
		}
}
inline void
Begin(float dt, bool active)
{
	auto &d = Data();
	d.overflow = false;
	d.contact = false;
	if(!active) {
		d.count = 0;
		return;
	}
	if(dt < 0.f) dt = 0.f;
	if(dt > .1f) dt = .1f;
	for(int i = 0; i < d.count;) {
		auto &e = d.entries[i];
		if(!e.touched) e.alpha += dt / 0.25f;
		e.touched = false;
		if(e.alpha >= 1.f)
			e = d.entries[--d.count];
		else
			++i;
	}
}
inline void
Touch(CEntity *entity)
{
	if(!entity) return;
	auto &d = Data();
	for(int i = 0; i < d.count; ++i)
		if(d.entries[i].entity == entity) {
			d.entries[i].alpha = .25f;
			d.entries[i].touched = true;
			return;
		}
	if(d.count < 32)
		d.entries[d.count++] = {entity, .25f, true};
	else
		d.overflow = true;
}
inline float
Alpha(CEntity *entity)
{
	if(PresetVisible(entity)) return Preset().opacity;
	auto &d = Data();
	for(int i = 0; i < d.count; ++i)
		if(d.entries[i].entity == entity) return d.entries[i].alpha;
	return 1.f;
}
struct CollectScope {
	bool previous;
	CollectScope() : previous(Data().collect) { Data().collect = true; }
	~CollectScope() { Data().collect = previous; }
};
} // namespace CameraOcclusion3DS
#endif
