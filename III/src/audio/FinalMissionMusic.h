#pragma once

namespace FinalMissionMusic
{
	extern int8 Enabled; // [Audio] FinalMissionBGM, default ON.
	bool IsFinaleActive(); // Mission lifecycle, independent of the audio setting.
	void BeginGTA3Finale();
	void FadeOutGTA3Finale();
	void EndGTA3Finale();
	void Update();
	void DisplayTrackName();
	bool IsRadioLocked();
}
