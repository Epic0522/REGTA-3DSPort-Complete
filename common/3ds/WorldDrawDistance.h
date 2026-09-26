#pragma once

// Shared III/VC budgets, matching the LCS 3DS world renderer.
namespace WorldDrawDistance3DS {
inline float FrontWeight(float distance, float forwardDot)
{
	if(distance <= 10.0f) return 1.0f;
	// Finish the transition outside the visible front hemisphere. Turning the
	// camera must not instantly change an object's draw/fade distance.
	float t = (forwardDot / distance + 0.65f) / 0.50f;
	if(t < 0.0f) t = 0.0f;
	if(t > 1.0f) t = 1.0f;
	t = t * t * (3.0f - 2.0f * t);
	float nearWeight = (20.0f - distance) / 10.0f;
	if(nearWeight < 0.0f) nearWeight = 0.0f;
	return nearWeight + (1.0f - nearWeight) * t;
}

inline float FrontScale(float reduced, float front, float distance, float forwardDot)
{
	if(front <= reduced) return reduced;
	return reduced + (front - reduced) * FrontWeight(distance, forwardDot);
}

inline float WorldFrontScale(float profile, float front, float distance,
                             float forwardDot, float radius, bool constrained)
{
	// A foreground allowance is not a second, larger global draw budget.
	// Preserve nearby silhouettes, then return to the selected profile smoothly.
	float allowance = 1.0f;
	if(constrained){
		const float surfaceDistance = distance > radius ? distance - radius : 0.0f;
		allowance = (100.0f - surfaceDistance) / 60.0f;
		if(allowance < 0.0f) allowance = 0.0f;
		if(allowance > 1.0f) allowance = 1.0f;
		allowance = allowance * allowance * (3.0f - 2.0f * allowance);
	}
	if(front < profile) front = profile;
	const float foreground = profile + (front - profile) * allowance;
	return FrontScale(profile * 0.50f, foreground, distance, forwardDot);
}

inline int FadeAlpha(float distance, float limit, int alpha)
{
	if(distance >= limit) return 0;
	const float remaining = limit - distance;
	return remaining < 8.0f ? int(alpha * remaining / 8.0f) : alpha;
}

inline bool HasNamePart(const char *name, const char *part)
{
	if(!name) return false;
	for(; *name; ++name) {
		const char *a = name, *b = part;
		while(*a && *b) {
			char c = *a;
			if(c >= 'A' && c <= 'Z') c += 'a' - 'A';
			if(c != *b) break;
			++a; ++b;
		}
		if(!*b) return true;
	}
	return false;
}

inline bool IsIsland(const char *name)
{
	return HasNamePart(name, "islandlod");
}

inline float Scale(const char *name, bool tree, bool streetProp)
{
	if(IsIsland(name)) return 1.0f;
	const bool shadow = HasNamePart(name, "shad");
	const bool vegetation = (HasNamePart(name, "tree") && !HasNamePart(name, "street")) ||
		HasNamePart(name, "palm") || HasNamePart(name, "pine") ||
		HasNamePart(name, "bush") || HasNamePart(name, "hedge") ||
		HasNamePart(name, "shrub") || HasNamePart(name, "fern") ||
		HasNamePart(name, "conifer");
	if(!shadow && (tree || vegetation)) return 1.2f;
	return streetProp ? 0.75f : 0.65f;
}

inline float SurfaceDistance(float originDistance, float centreDistance,
                            float radius, bool building, bool island)
{
	if(building && !island && radius >= 16.0f)
		return centreDistance > radius ? centreDistance - radius : 0.0f;
	return originDistance;
}
}
