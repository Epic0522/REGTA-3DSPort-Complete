#pragma once
#ifdef _3DS
#include <math.h>
namespace CameraComfort3DS
{
inline float
ClampValue(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}
inline float
Approach(float old, float goal, float maxStep)
{
	return old + ClampValue(goal - old, -maxStep, maxStep);
}
inline float
Angle(float old, float goal, float maxStep)
{
	float d = fmodf(goal - old + 3.141592654f, 6.283185307f);
	if(d < 0.f) d += 6.283185307f;
	return old + ClampValue(d - 3.141592654f, -maxStep, maxStep);
}
inline CVector
ApproachVector(const CVector &old, const CVector &goal, float maxStep)
{
	CVector d = goal - old;
	float length = d.Magnitude();
	return length > maxStep && length > 0.f ? old + d * (maxStep / length) : goal;
}
// Exponential settling removes the abrupt stop of a pure speed limiter.
inline float
EaseStep(float error, float dt, float speed, float gain)
{
	return ClampValue(error * gain, -speed * dt, speed * dt);
}
inline float
AcceleratedStep(float error, float dt, float speed, float gain, float rate, float acceleration, float &nextRate)
{
	const float desiredRate = ClampValue(error * gain / dt, -speed, speed);
	nextRate = Approach(rate, desiredRate, acceleration * dt);
	float step = (rate + nextRate) * .5f * dt;
	if(step * error <= 0.f) {
		nextRate = 0.f;
		return 0.f;
	}
	if(fabsf(step) >= fabsf(error)) {
		nextRate = 0.f;
		return error;
	}
	return step;
}
inline void
LevelHorizon(const CVector &front, CVector &up)
{
	CVector right = CrossProduct(front, CVector(0, 0, 1));
	if(right.MagnitudeSqr() < 0.000001f) right = CrossProduct(front, CVector(0, 1, 0));
	right.Normalise();
	up = CrossProduct(right, front);
	up.Normalise();
}
inline bool
SmoothInteraction(bool manual, bool action, bool transition)
{
	return !manual && (action || transition);
}
// A door-opening event begins at the last displayed pose, not the new ped pivot.
struct DoorTransition {
	bool wasExiting = false, active = false;
	float elapsed = 0.f;
	CVector source, target;
	void Reset()
	{
		wasExiting = active = false;
		elapsed = 0.f;
	}
	bool Apply(bool exiting, bool manual, float dt, const CVector &previousSource, const CVector &previousTarget, CVector &nextSource, CVector &nextTarget)
	{
		if(exiting && !wasExiting) {
			source = previousSource;
			target = previousTarget;
			elapsed = 0.f;
			active = true;
		}
		wasExiting = exiting;
		if(manual) active = false;
		if(!active) return false;
		float t = ClampValue(elapsed / .4f, 0.f, 1.f);
		float weight = t * t * (3.f - 2.f * t);
		nextSource = source + (nextSource - source) * weight;
		nextTarget = target + (nextTarget - target) * weight;
		elapsed += ClampValue(dt, 0.f, .1f);
		if(t >= 1.f) active = false;
		return true;
	}
};
// The stock pedestrian camera remains useful as a moving destination during an
// exit, but it occasionally emits a collision-compressed low upward shot. Keep
// the last trustworthy destination relative to the ped and reject only those
// bad frames; the stored destination still translates with a moving player.
struct ExitDestination {
	bool valid = false;
	CVector offset, aim;
	void Reset() { valid = false; }
	void Remember(const CVector &anchor, const CVector &source, const CVector &target)
	{
		offset = source - anchor;
		aim = target - anchor;
		valid = true;
	}
	bool Apply(const CVector &displayedSource, const CVector &displayedFront, const CVector &anchor, CVector &source, CVector &front)
	{
		CVector rawOffset = source - anchor;
		const float distance = rawOffset.Magnitude();
		const CVector rawFocus = source + front * Max(.5f, distance);
		const bool lowUp = rawOffset.z < .25f && front.z > .08f;
		const bool sane = distance > 1.f && distance < 30.f && !lowUp && (rawFocus - (anchor + CVector(0, 0, .8f))).Magnitude() < 8.f;
		if(sane) {
			offset = rawOffset;
			aim = rawFocus - anchor;
			valid = true;
			return false;
		}
		if(!valid) {
			offset = displayedSource - anchor;
			const float shownDistance = Max(1.f, offset.Magnitude());
			aim = displayedSource + displayedFront * shownDistance - anchor;
			if(offset.Magnitude() < 1.f || (offset.z < .25f && displayedFront.z > .08f)) {
				CVector away(-displayedFront.x, -displayedFront.y, 0.f);
				if(away.MagnitudeSqr() < .0001f)
					away = CVector(0, -1, 0);
				else
					away.Normalise();
				offset = away * 4.f + CVector(0, 0, 1.5f);
				aim = CVector(0, 0, .8f);
			}
			valid = true;
		}
		source = anchor + offset;
		front = anchor + aim - source;
		if(front.MagnitudeSqr() < .0001f)
			front = CVector(0, 1, 0);
		else
			front.Normalise();
		return true;
	}
};
struct Motion {
	bool valid = false;
	CVector anchor, offset, aim;
	float yawRate = 0.f, pitchRate = 0.f, radialRate = 0.f;
	mutable float nextYawRate = 0.f, nextPitchRate = 0.f, nextRadialRate = 0.f;
	mutable bool pending = false;
	mutable CVector proposed;
	void Reset()
	{
		valid = false;
		pending = false;
		yawRate = pitchRate = radialRate = 0.f;
	}
	void Commit(const CVector &a, const CVector &source, const CVector &target)
	{
		if(pending && (source - proposed).MagnitudeSqr() < 0.000001f) {
			yawRate = nextYawRate;
			pitchRate = nextPitchRate;
			radialRate = nextRadialRate;
		} else
			yawRate = pitchRate = radialRate = 0.f;
		pending = false;
		anchor = a;
		offset = source - a;
		aim = target - a;
		valid = true;
	}
	bool Previous(const CVector &a, CVector &source, CVector &target) const
	{
		if(!valid || (a - anchor).MagnitudeSqr() > 144.f) return false;
		source = a + offset;
		target = a + aim;
		return true;
	}
	void Rebase(const CVector &a)
	{
		if(valid) {
			offset = anchor + offset - a;
			aim = anchor + aim - a;
			anchor = a;
		}
		pending = false;
		yawRate = pitchRate = radialRate = 0.f;
	}
	bool Direct(const CVector &a, const CVector &source, const CVector &target, CVector &outSource, CVector &outTarget)
	{
		pending = false;
		yawRate = pitchRate = radialRate = 0.f;
		outSource = source;
		outTarget = target;
		if(!valid || (a - anchor).MagnitudeSqr() > 144.f) return false;
		CVector delta = source - a;
		float distance = delta.Magnitude(), oldDistance = offset.Magnitude();
		if(distance < oldDistance && oldDistance >= .3f) outSource = a + (distance > .001f ? delta * (oldDistance / distance) : offset);
		return true;
	}
	bool Candidate(const CVector &a, const CVector &source, const CVector &target, float dt, bool fast, CVector &outSource, CVector &outTarget) const
	{
		pending = false;
		if(!valid || dt <= 0.f || dt > 0.25f) return false;
		CVector desired = source - a;
		const float distance = desired.Magnitude(), oldDistance = offset.Magnitude();
		if((a - anchor).MagnitudeSqr() > 144.f || oldDistance < 0.3f || distance > 50.f || oldDistance > 50.f) return false;
		if(distance < 0.3f) desired = offset; // Preserve a direction even if collision collapsed the raw pose.
		dt = ClampValue(dt, 0.f, 0.1f);
		const float gain = fast ? 1.f : 1.f - expf(-dt / .22f);
		const float oldYaw = atan2f(offset.y, offset.x);
		const float goalYaw = Angle(oldYaw, atan2f(desired.y, desired.x), 3.141592654f);
		nextYawRate = nextPitchRate = nextRadialRate = 0.f;
		const float yaw = oldYaw + (fast ? EaseStep(goalYaw - oldYaw, dt, 3.141592654f, 1.f)
		                                 : AcceleratedStep(goalYaw - oldYaw, dt, 1.221730476f, gain, yawRate, 4.f, nextYawRate));
		const float oldPitch = atan2f(offset.z, sqrtf(offset.x * offset.x + offset.y * offset.y));
		const float goalPitch = atan2f(desired.z, sqrtf(desired.x * desired.x + desired.y * desired.y));
		const float pitch = oldPitch + (fast ? EaseStep(goalPitch - oldPitch, dt, 1.570796327f, 1.f)
		                                     : AcceleratedStep(goalPitch - oldPitch, dt, .785398163f, gain, pitchRate, 3.f, nextPitchRate));
		// Manual orbit must not use an obstacle to ratchet the radius inward.
		const float goalRadius = fast && distance < oldDistance ? oldDistance : distance;
		const float radialSpeed = fast ? (goalRadius < oldDistance ? 4.f : 6.f) : (goalRadius < oldDistance ? 3.f : 4.f);
		const float radius = oldDistance + (fast ? EaseStep(goalRadius - oldDistance, dt, radialSpeed, 1.f)
		                                         : AcceleratedStep(goalRadius - oldDistance, dt, radialSpeed, gain, radialRate, 10.f, nextRadialRate));
		outSource = a + CVector(cosf(yaw) * cosf(pitch), sinf(yaw) * cosf(pitch), sinf(pitch)) * radius;
		const float aimStep = ClampValue((target - a - aim).Magnitude() * gain, 0.f, (fast ? 4.f : 3.f) * dt);
		outTarget = a + ApproachVector(aim, target - a, aimStep);
		proposed = outSource;
		pending = true;
		return (outSource - outTarget).MagnitudeSqr() > 0.04f &&
		       ((outSource - source).MagnitudeSqr() > 0.000001f || (outTarget - target).MagnitudeSqr() > 0.000001f);
	}
};
} // namespace CameraComfort3DS
#endif
