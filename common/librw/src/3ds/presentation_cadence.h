#pragma once
#include <stdint.h>

namespace rw
{
namespace c3d
{
// Pace complete stereo pairs, not camera passes or simulation updates.
// A late pair is published immediately; no accumulated catch-up deadline.
struct PresentationCadence {
	uint64_t last = 0;
	bool valid = false;
	uint64_t delay(uint64_t now, uint64_t period, bool stereo)
	{
		if(!stereo || (valid && (now < last || now - last > period * 8))) {
			valid = false;
			return 0;
		}
		if(!valid || now - last >= period) return 0;
		return period - (now - last);
	}
	void presented(uint64_t now, bool stereo)
	{
		last = now;
		valid = stereo;
	}
};
} // namespace c3d
} // namespace rw
