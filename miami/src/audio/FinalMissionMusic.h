#pragma once

namespace FinalMissionMusic
{
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
