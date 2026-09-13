#pragma once

class CVehicle;

namespace FinalMissionMusic
{
void ArmLCSFinaleBoatCheckpoint();
void CancelLCSFinaleBoatCheckpoint();
bool ConsumeLCSFinaleBoatCheckpointRequest();
	void BeginLCSFinale();
	void StartLCSFinaleFM();
	void SwitchLCSFinaleToLoop();
	void EnterLCSBoatChase();
	void BeginLCSLighthouseTransition();
	void BeginLCSLighthouseClimax();
	void BeginLCSLighthouseBattle();
	bool ShouldSuppressLCSBoatExplosion(CVehicle *vehicle);
	void FadeOutLCSFinale();
	void EndLCSFinale();
	void Update();
	void DisplayTrackName();
	bool IsRadioLocked();
}
