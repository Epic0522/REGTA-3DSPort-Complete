#pragma once

namespace TrafficSpawn3DS {
inline bool PreferNearby(unsigned frame) { return (frame & 3) != 0; }
inline float NearbyDistance() { return 32.0f; }
inline float OffscreenRetention(bool onFoot, bool ordinaryCar, bool visible, float original)
{
	// Camera-look flags otherwise preserve distant unseen traffic and fill the
	// same small population budget that the nearby roads need while on foot.
	return onFoot && ordinaryCar && !visible && original > 60.0f ? 60.0f : original;
}
}
