#pragma once

// Shared III/VC budgets, matching the LCS 3DS world renderer.
namespace WorldDrawDistance3DS {
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
