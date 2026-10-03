#pragma once

// PS2-inspired 3DS budgets, not measured retail PS2/PC distance ratios.
// Skyline clipping, individual model LOD and dynamic actors are distinct.
namespace PS2Graphics3DS {
enum Game { GTAIII = 0, ViceCity = 1, LibertyCityStories = 2 };
static const bool DefaultEnabled = true;
static const float DetailRangeScale = 0.85f;
inline float Minimum(float a, float b) { return a < b ? a : b; }
inline float Maximum(float a, float b) { return a > b ? a : b; }

inline float WorldRangeScale(int game)
{
	switch(game){
	case GTAIII: return 0.83f;
	case LibertyCityStories: return 0.76f;
	default: return 0.80f;
	}
}

inline float FarClip(float authored, float pressureScale, bool enabled, int game)
{
	const float current = Minimum(authored, Maximum(180.0f, authored * pressureScale));
	if(!enabled) return current;
	// Trim the back of the skyline modestly; III needs less relief than VC/LCS.
	// Keep this relative to the same weather/pressure profile, without a hard cap.
	return current * WorldRangeScale(game);
}

inline float WorldOpacity(float nearestDepth, float range)
{
	// Finish dissolving ordinary distant buildings before the budget boundary.
	return Maximum(0.0f, Minimum(1.0f, (range - nearestDepth) / Maximum(range * 0.10f, 1.0f)));
}

inline float FogStart(float authored, float farClip, float pressureScale, bool enabled)
{
	if(!enabled) return Minimum(authored, Maximum(120.0f, authored * pressureScale));
	// Near half stays clear; fog grows over the rear distance band.
	return Maximum(farClip * 0.55f, Minimum(authored * pressureScale, farClip * 0.65f));
}

inline float FogVisibility(float distance, float start, float end)
{
	const float t = Maximum(0.0f, Minimum(1.0f, (distance - start) / Maximum(end - start, 1.0f)));
	// Retain 15% scene contrast at the far plane, so fog leaves silhouettes.
	return 1.0f - 0.85f * t;
}

inline float SilhouetteWeight(float nearestDepth, float start, float end, int game = GTAIII)
{
	const float span = Maximum(end - start, 1.0f);
	// VC/LCS: the facing coast can retain some texture while the rear buildings
	// finish switching to silhouettes. III keeps surface detail further into fog.
	// The nearest bound must enter fog first, protecting large nearby faces.
	const float begin = game == GTAIII ? 0.25f : 0.0f;
	const float finish = game == GTAIII ? 0.70f : 0.40f;
	return Maximum(0.0f, Minimum(1.0f, (nearestDepth - start - span * begin) / (span * (finish - begin))));
}

inline float FogColourScale(float red, float green, float blue, const float *gain)
{
	// Colour grading must not saturate the fog to white and erase silhouettes.
	const float peak = Maximum(red * gain[0], Maximum(green * gain[1], blue * gain[2]));
	return peak > 204.0f ? 204.0f / peak : 1.0f;
}

// Algebraic form of the desktop colourfilterIII/VC/LCS fragment shaders.
// Separating colour from history avoids tinting the retained frame again.
inline float ColourGain(int game, float authoredChannel, float authoredAlpha = 30.0f)
{
	const float colour = Maximum(0.0f, Minimum(1.0f, authoredChannel / 255.0f));
	if(game == LibertyCityStories) return 1.0f + colour * 0.35f; // Gentler LCS foreground grade
	// III's timecycle supplies its own strength; VC's accepted filter is unchanged.
	const float alpha = game == GTAIII ? Maximum(0.0f, Minimum(1.0f, authoredAlpha / 255.0f)) : 30.0f / 255.0f;
	const float feedback = game == ViceCity ? Minimum(2.0f * colour, 1.0f) * alpha + 2.0f * colour : colour * alpha;
	float gain = 1.0f;
	for(int i = 0; i < 5; ++i) gain = (1.0f - alpha) + feedback * gain;
	// PICA's one-stage multiplier tops out at 4; retain highlight saturation
	// without several more full-screen passes at extreme scripted colours.
	return Minimum(gain, 4.0f);
}

inline float FogColourCompensation(int game, float authoredChannel, float gain)
{
	// Preserve LCS's accepted distant blue haze independently of its weaker
	// foreground grade. Other games retain their existing fog/grade relationship.
	if(game != LibertyCityStories) return 1.0f;
	const float colour = Maximum(0.0f, Minimum(1.0f, authoredChannel / 255.0f));
	return (1.0f + colour) / gain;
}

inline float OccupantOpacity(float distanceSquared, float range)
{
	// Compare squared distances so each passenger does not need another sqrt.
	const float inner = range - 8.0f;
	return Maximum(0.0f, Minimum(1.0f, (range * range - distanceSquared) /
		(range * range - inner * inner)));
}
}
