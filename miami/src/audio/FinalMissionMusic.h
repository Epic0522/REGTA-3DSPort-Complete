#pragma once

namespace FinalMissionMusic
{
	extern int8 Enabled; // [Audio] FinalMissionBGM, default ON.
	bool IsFinaleActive(); // Mission lifecycle, independent of the audio setting.
	void BeginVCFinale();
	void ObserveVCMissionAudioLoaded(const char *name, uint8 slot);
	void ObserveVCMissionAudioPlayed(uint8 slot);
	void ObserveVCMissionAudioFinished(uint8 slot);
	void ObserveVCTextKey(const char *key);
	void ObserveVCCutsceneLoaded(const char *name);
	void EndVCFinale();
	void Update();
	void DisplayTrackName();
	bool IsRadioLocked();
}
