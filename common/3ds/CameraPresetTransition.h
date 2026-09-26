#pragma once
#ifdef _3DS
#include <math.h>
namespace CameraPreset3DS
{
inline float
BoundPreset(float x, float lo, float hi)
{
	return x < lo ? lo : (x > hi ? hi : x);
}
inline float
Wrap(float a)
{
	a = fmodf(a + 3.141592654f, 6.283185307f);
	if(a < 0) a += 6.283185307f;
	return a - 3.141592654f;
}
inline float
Dot(const CVector &a, const CVector &b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
struct Pose {
	CVector source, front, up;
	float fov, nearClip;
};
struct Angles {
	float yaw, pitch, roll;
};
inline Angles
Read(const Pose &p)
{
	CVector f = p.front;
	f.Normalise();
	CVector right = CrossProduct(f, p.up);
	right.Normalise();
	float yaw = f.x * f.x + f.y * f.y > .000001f ? atan2f(f.y, f.x) : atan2f(right.y, right.x) + 1.570796327f;
	float pitch = asinf(BoundPreset(f.z, -1.f, 1.f));
	CVector levelUp(-sinf(pitch) * cosf(yaw), -sinf(pitch) * sinf(yaw), cosf(pitch));
	CVector levelRight(sinf(yaw), -cosf(yaw), 0.f);
	return {yaw, pitch, atan2f(Dot(p.up, levelRight), Dot(p.up, levelUp))};
}
inline void
Orient(Pose &p, const Angles &a)
{
	p.front = CVector(cosf(a.pitch) * cosf(a.yaw), cosf(a.pitch) * sinf(a.yaw), sinf(a.pitch));
	CVector up(-sinf(a.pitch) * cosf(a.yaw), -sinf(a.pitch) * sinf(a.yaw), cosf(a.pitch));
	CVector right(sinf(a.yaw), -cosf(a.yaw), 0.f);
	p.up = up * cosf(a.roll) + right * sinf(a.roll);
}
struct Transition {
	bool valid = false, active = false, special = false;
	int key = 0;
	float elapsed = 0, duration = .65f, progress = 0;
	Pose displayed;
	CVector displayedAnchor;
	bool tracked = false, orbit = false, gentle = false;
	CVector startSource, startAnchor, startFocus;
	Angles startAngles;
	float startRadius = 0;
	float fovOffset = 0, nearOffset = 0;
	void Reset() { valid = active = false; }
	bool Apply(int nextKey, bool nextSpecial, bool eligible, float dt, Pose &pose, const CVector *anchor = nullptr, bool track = false,
	           bool slowExit = false)
	{
		if(!eligible || dt < 0.f) {
			Reset();
			return false;
		}
		if(!valid) {
			valid = true;
			key = nextKey;
			special = nextSpecial;
			displayed = pose;
			if(anchor) displayedAnchor = *anchor;
			tracked = track;
			return false;
		}
		if(nextKey == key && !active) {
			special = nextSpecial;
			displayed = pose;
			if(anchor) displayedAnchor = *anchor;
			tracked = track;
			return false;
		}
		dt = BoundPreset(dt, 0.f, .1f);
		const bool restarting = nextKey != key;
		Angles raw = Read(pose);
		if(nextKey != key) {
			const Angles previous = Read(displayed);
			startAngles = previous;
			startSource = displayed.source;
			startAnchor = anchor ? *anchor : CVector(0, 0, 0);
			orbit = anchor && track && tracked;
			if(orbit) {
				float oldRadius = BoundPreset(Dot(displayedAnchor - displayed.source, displayed.front), .5f, 200.f);
				startRadius = oldRadius;
				startFocus = displayed.source + displayed.front * oldRadius;
			}
			fovOffset = displayed.fov - pose.fov;
			nearOffset = displayed.nearClip - pose.nearClip;
			gentle = slowExit || (active && gentle);
			elapsed = 0;
			duration = (special || nextSpecial) ? .9f : .65f;
			// Returning to pedestrian follow should finish in the same nominal time as
			// entering the normal rear vehicle follow. Per-frame comfort caps below
			// still prevent an extreme angle or target change from snapping.
			if(gentle) duration = .65f;
			active = true;
			key = nextKey;
		}
		special = nextSpecial;
		if(!active) {
			displayed = pose;
			if(anchor) displayedAnchor = *anchor;
			tracked = track;
			return false;
		}
		const float t = BoundPreset(elapsed / duration, 0.f, 1.f);
		progress = t;
		if(t >= 1.f && !gentle) {
			active = false;
			displayed = pose;
			if(anchor) displayedAnchor = *anchor;
			tracked = track;
			return false;
		}
		const float weight = 1.f - t * t * t * (t * (t * 6.f - 15.f) + 10.f);
		const CVector rawSource = pose.source, rawFront = pose.front;
		const CVector carriedStart = startSource + (anchor ? *anchor - startAnchor : CVector(0, 0, 0));
		pose.source = carriedStart * weight + pose.source * (1.f - weight);
		Orient(pose, {startAngles.yaw + Wrap(raw.yaw - startAngles.yaw) * (1.f - weight),
		              startAngles.pitch + (raw.pitch - startAngles.pitch) * (1.f - weight),
		              startAngles.roll + Wrap(raw.roll - startAngles.roll) * (1.f - weight)});
		if(orbit && anchor) {
			// Interpolate the sight-line focus and orbit radius together: never blend an
			// unrelated world position and look angle that send the subject off-screen.
			const float radius = BoundPreset(Dot(*anchor - rawSource, rawFront), .5f, 200.f);
			const CVector focus = (startFocus + *anchor - startAnchor) * weight + (rawSource + rawFront * radius) * (1.f - weight);
			pose.source = focus - pose.front * BoundPreset(startRadius * weight + radius * (1.f - weight), .5f, 200.f);
		}
		pose.fov += fovOffset * weight;
		pose.nearClip = BoundPreset(pose.nearClip + nearOffset * weight, .01f, 10.f);
		if(gentle) {
			const Angles before = Read(displayed), wanted = Read(pose);
			const float dy = Wrap(wanted.yaw - before.yaw), dp = wanted.pitch - before.pitch;
			const CVector motion = anchor && !restarting ? *anchor - displayedAnchor : CVector(0, 0, 0);
			const CVector previousSource = displayed.source + motion;
			bool limited = false;
			if(orbit && anchor) {
				const float oldRadius = BoundPreset(Dot(displayedAnchor - displayed.source, displayed.front), .5f, 200.f);
				const float radius = BoundPreset(Dot(*anchor - pose.source, pose.front), .5f, 200.f);
				// Share a travel budget between orbit, radius and focus changes. Move
				// around the focal point, never clamp position separately from direction.
				const float angular = 1.5f;
				const float yawStep = angular * dt;
				const float pitchStep = 1.0f * dt;
				const CVector oldFocus = previousSource + displayed.front * oldRadius;
				CVector focus = pose.source + pose.front * radius;
				CVector focusMove = focus - oldFocus;
				const float focusDistance = focusMove.Magnitude();
				const float dr = radius - oldRadius;
				limited = fabsf(dy) > yawStep || fabsf(dp) > pitchStep || fabsf(dr) > 14.f * dt || focusDistance > 8.f * dt;
				if(focusDistance > 8.f * dt && focusDistance > 0.f) focus = oldFocus + focusMove * (8.f * dt / focusDistance);
				Orient(pose,
				       {before.yaw + BoundPreset(dy, -yawStep, yawStep), before.pitch + BoundPreset(dp, -pitchStep, pitchStep), wanted.roll});
				pose.source = focus - pose.front * (oldRadius + BoundPreset(dr, -14.f * dt, 14.f * dt));
			} else {
				CVector travel = pose.source - previousSource;
				const float distance = travel.Magnitude();
				limited = fabsf(dy) > 1.5f * dt || fabsf(dp) > dt || distance > 14.f * dt;
				if(distance > 14.f * dt && distance > 0.f) pose.source = previousSource + travel * (14.f * dt / distance);
				Orient(pose, {before.yaw + BoundPreset(dy, -1.5f * dt, 1.5f * dt), before.pitch + BoundPreset(dp, -dt, dt), wanted.roll});
			}
			if(t >= 1.f && !limited) active = false;
		}
		displayed = pose;
		if(anchor) displayedAnchor = *anchor;
		tracked = track;
		elapsed += BoundPreset(dt, 0.f, .1f);
		return true;
	}
};
struct OpacityEnvelope {
	bool valid = false;
	int key = 0;
	float start = 1.f, value = 1.f;
	void Reset()
	{
		valid = false;
		start = value = 1.f;
	}
	float Apply(int nextKey, bool active, float progress, bool hood)
	{
		if(!valid) {
			valid = true;
			key = nextKey;
			start = value = 1.f;
		}
		if(nextKey != key) {
			start = value;
			key = nextKey;
		}
		if(!active) {
			value = hood ? 0.f : 1.f;
			return value;
		}
		const float t = BoundPreset(progress, 0.f, 1.f);
		float u = t < .5f ? t * 2.f : (t - .5f) * 2.f;
		u = u * u * (3.f - 2.f * u);
		value = t < .5f ? start + (.4f - start) * u : .4f + ((hood ? 0.f : 1.f) - .4f) * u;
		return value;
	}
};

// The original car top-down uses (-.01,-.01,-1) with north-up. Near the
// pole that tiny horizontal bias otherwise decomposes into ~45 degrees roll.
inline void
CanonicalTopDown(Pose &pose)
{
	pose.front = CVector(0, 0, -1);
	pose.up = CVector(0, 1, 0);
}
inline bool
TopDownFrustum(float frontZ)
{
	return frontZ < -.75f;
}
inline bool
NearVehicle(const CVector &local, const CVector &lo, const CVector &hi)
{
	const float margin = .65f;
	return local.x > lo.x - margin && local.x < hi.x + margin && local.y > lo.y - margin && local.y < hi.y + margin && local.z > lo.z - margin &&
	       local.z < hi.z + margin;
}
struct VehicleOpacity {
	float value = 1.f;
	void Reset() { value = 1.f; }
	float Update(float target, float dt)
	{
		const float step = BoundPreset(dt, 0.f, .1f) * 2.f;
		value += BoundPreset(target - value, -step, step);
		value = BoundPreset(value, 0.f, 1.f);
		return value;
	}
};

struct ManualDistance {
	bool active = false;
	float radius = 0, settled = 0;
	void Begin(const Pose &shown, const CVector &anchor)
	{
		radius = BoundPreset((shown.source - anchor).Magnitude(), .5f, 200.f);
		active = true;
		settled = 0;
	}
	bool Apply(Pose &raw, const CVector &anchor, float dt)
	{
		if(!active) return false;
		const float goal = BoundPreset((raw.source - anchor).Magnitude(), .5f, 200.f);
		radius += BoundPreset(goal - radius, -14.f * BoundPreset(dt, 0.f, .1f), 14.f * BoundPreset(dt, 0.f, .1f));
		CVector ray = raw.source - anchor;
		ray.Normalise();
		raw.source = anchor + ray * radius;
		if(fabsf(radius - goal) < .001f)
			settled += BoundPreset(dt, 0.f, .1f);
		else
			settled = 0;
		if(settled >= .4f) active = false;
		return true;
	}
};
// Entry and vehicle-preset final-output guard. Native zoom can reset while the pose blend is
// still running, so guarding only its completion leaves the actual jump exposed.
struct CinematicHandoff {
	bool waiting = false;
	bool Update(bool wasCinema, bool isCinema, bool restorePending)
	{
		if(isCinema || !restorePending)
			waiting = false;
		else if(wasCinema)
			waiting = true;
		return waiting;
	}
};
struct FollowSelection {
	bool normal = false;
	int zoom = -1;
	bool Update(bool nextNormal, int nextZoom)
	{
		const bool changed = nextNormal && (!normal || zoom != nextZoom);
		normal = nextNormal;
		zoom = nextZoom;
		return changed;
	}
};
struct EntryDistance {
	bool active = false;
	float radius = 0, settled = 0;
	void CancelForManual(bool input, bool overrideActive)
	{
		if(input || overrideActive) active = false;
	}
	void Begin(const Pose &shown, const CVector &anchor)
	{
		radius = (shown.source - anchor).Magnitude();
		settled = 0;
		active = true;
	}
	void Apply(Pose &pose, const CVector &anchor, float dt, bool busy)
	{
		if(!active) return;
		dt = BoundPreset(dt, 0.f, .1f);
		CVector ray = pose.source - anchor;
		const float goal = ray.Magnitude();
		const float next = goal < radius ? goal : BoundPreset(goal, 0.f, radius + 6.f * dt);
		// Allow collision-driven inward correction immediately; only retreat is capped.
		if(goal > 0.0001f) pose.source = anchor + ray * (next / goal);
		radius = next;
		if(!busy && fabsf(goal - next) < .001f)
			settled += dt;
		else
			settled = 0;
		if(settled >= 1.f) active = false;
	}
};
struct DisplayState {
	bool moving = false;
};
inline DisplayState &
Display()
{
	static DisplayState state;
	return state;
}
// XY projection of the entire camera pyramid; at most five hull vertices.
// Unlike a forward triangle it also covers ground below/behind a high camera.
struct Point2 {
	float x, y;
};
inline float
Side(Point2 a, Point2 b, Point2 c)
{
	return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
inline int
FrustumHull(const CVector *corners, Point2 *out)
{
	Point2 p[5];
	for(int i = 0; i < 5; ++i) p[i] = {corners[i].x, corners[i].y};
	for(int i = 1; i < 5; ++i) {
		Point2 v = p[i];
		int j = i;
		while(j > 0 && (p[j - 1].x > v.x || (p[j - 1].x == v.x && p[j - 1].y > v.y))) {
			p[j] = p[j - 1];
			--j;
		}
		p[j] = v;
	}
	int k = 0;
	for(int i = 0; i < 5; ++i) {
		while(k >= 2 && Side(out[k - 2], out[k - 1], p[i]) <= 0) --k;
		out[k++] = p[i];
	}
	int lower = k + 1;
	for(int i = 3; i >= 0; --i) {
		while(k >= lower && Side(out[k - 2], out[k - 1], p[i]) <= 0) --k;
		out[k++] = p[i];
	}
	return k - 1;
}

} // namespace CameraPreset3DS
#endif
