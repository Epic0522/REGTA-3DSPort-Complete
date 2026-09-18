#include "common.h"

#include "FinalMissionMusic.h"

#include "Camera.h"
#include "CutsceneMgr.h"
#include "Font.h"
#include "General.h"
#include "Replay.h"
#include "Timer.h"
#include "sampman.h"

namespace FinalMissionMusic
{
int8 Enabled = 1;
namespace
{
	enum eTrackState
	{
		TRACK_STOPPED,
		TRACK_FM,
		TRACK_SWITCHING,
		TRACK_LOOP,
		TRACK_CLIMAX_FADING,
		TRACK_CLIMAX_SILENT,
		TRACK_FADING
	};

	static const char *VC_FM_PATH = "Audio\\music\\SELF_FM.WAV";
	static const char *VC_LOOP_PATH = "Audio\\music\\SELF_LOOP.WAV";
	static const uint32 FADE_OUT_TIME_MS = 1000;
	static const uint32 CLIMAX_FADE_OUT_TIME_MS = 1500;

	bool gVCFinaleActive;
	eTrackState gTrackState = TRACK_STOPPED;
	uint32 gFadeStartTime;
	bool gStreamStarted;
	uint8 gTrackNameFrames;
	int8 gLanceLineSlot = -1;
	bool gLanceLinePlayed;
	bool gLanceBattleStarted;
	uint8 gLoopStartDelay;

	bool
	StartTrack(const char *path, bool loop)
	{
		SampleManager.StopMissionMusicStream();
		gStreamStarted = Enabled && SampleManager.StartMissionMusicStream(path, loop);
		return gStreamStarted;
	}

	void
	SwitchToLoop(bool forceRestart = false)
	{
		if (!gVCFinaleActive || (!forceRestart && gTrackState != TRACK_FM))
			return;
		/* The script asks whether FIN_3 has finished from inside its mission-audio
		 * command. Rebuilding another OpenAL stream in that call stack can reuse
		 * sources before the 3DS backend has observed their stopped state. Tear the
		 * FM stream down now and start LOOP from the regular update on a later frame. */
		SampleManager.StopMissionMusicStream();
		gStreamStarted = false;
		gTrackState = TRACK_SWITCHING;
		gLoopStartDelay = 1;
	}

	void
	BeginClimaxFade()
	{
		if (!gVCFinaleActive || gTrackState == TRACK_CLIMAX_FADING ||
		    gTrackState == TRACK_CLIMAX_SILENT)
			return;
		if (!gStreamStarted || gTrackState == TRACK_STOPPED) {
			gTrackState = TRACK_CLIMAX_SILENT;
			return;
		}
		gTrackState = TRACK_CLIMAX_FADING;
		gFadeStartTime = CTimer::GetTimeInMilliseconds();
	}

	void
	FadeOut()
	{
		if (!gVCFinaleActive || gTrackState == TRACK_STOPPED || gTrackState == TRACK_FADING)
			return;
		gTrackState = TRACK_FADING;
		gFadeStartTime = CTimer::GetTimeInMilliseconds();
	}

	void
	ApplyVolume(uint8 volume)
	{
		/* Self Control is mastered above the native mission mix. Gameplay uses
		 * 80%; cutscenes halve that result to 40% of the previous full level. */
		volume = (uint32(volume) * 4) / 5;
		if (CCutsceneMgr::IsRunning() || TheCamera.m_WideScreenOn)
			volume /= 2;
		SampleManager.SetMissionMusicStreamVolume(volume);
	}
}

void
BeginVCFinale()
{
	EndVCFinale();
	gVCFinaleActive = true;
	gTrackState = TRACK_FM;
	gTrackNameFrames = 60;
	gLanceBattleStarted = false;
	gLoopStartDelay = 0;
	StartTrack(VC_FM_PATH, false);
}

void
ObserveVCMissionAudioLoaded(const char *name, uint8 slot)
{
	if (!gVCFinaleActive || CGeneral::faststricmp(name, "FIN_3"))
		return;
	gLanceLineSlot = slot;
	gLanceLinePlayed = false;
	gLoopStartDelay = 0;
}

void
ObserveVCMissionAudioPlayed(uint8 slot)
{
	if (gVCFinaleActive && gLanceLineSlot == slot) {
		gLanceLinePlayed = true;
		/* FIN_3 is Lance's "No one to cover your ass now" line.  It starts
		 * the scripted reveal before the rooftop fight, so fade the current
		 * section here and remain silent until FIN_B1 starts gameplay. */
		BeginClimaxFade();
	}
}

void
ObserveVCMissionAudioFinished(uint8 slot)
{
	if (!gVCFinaleActive || !gLanceLinePlayed || gLanceLineSlot != slot)
		return;
	gLanceLineSlot = -1;
	gLanceLinePlayed = false;
}

void
ObserveVCTextKey(const char *key)
{
	if (!gVCFinaleActive || gLanceBattleStarted || CGeneral::faststricmp(key, "FIN_B1"))
		return;
	/* FIN_B1 ("Go and kill Lance Vance the backstabber") is emitted on the
	 * first gameplay frame after the reveal.  Start the edited climax here,
	 * independently of mission-audio stream completion and cutscene skipping. */
	gLanceBattleStarted = true;
	SwitchToLoop(true);
}

void
ObserveVCCutsceneLoaded(const char *name)
{
	/* The ending scene is the first native event after Sonny dies.  FIN_3 is
	 * only Lance's mission-audio line; the actual cutscene asset is FINALE. */
	if (gVCFinaleActive && !CGeneral::faststricmp(name, "FINALE"))
		FadeOut();
}

void
EndVCFinale()
{
	if (gVCFinaleActive || gStreamStarted)
		SampleManager.StopMissionMusicStream();
	gVCFinaleActive = false;
	gTrackState = TRACK_STOPPED;
	gFadeStartTime = 0;
	gStreamStarted = false;
	gTrackNameFrames = 0;
	gLanceLineSlot = -1;
	gLanceLinePlayed = false;
	gLanceBattleStarted = false;
}

void
Update()
{
	if (!gVCFinaleActive)
		return;

	// Music is optional; LCS mission repairs above must continue to run.
	static bool wasEnabled = true;
	if (!Enabled) {
		if (gStreamStarted) SampleManager.StopMissionMusicStream();
		gStreamStarted = false;
		wasEnabled = false;
		return;
	}
	if (!wasEnabled) {
		wasEnabled = true;
		if (!gStreamStarted && gTrackState == TRACK_FM) StartTrack(VC_FM_PATH, false);
		else if (!gStreamStarted && gTrackState == TRACK_LOOP) StartTrack(VC_LOOP_PATH, true);
	}
	const bool manuallyPaused = CTimer::GetIsUserPaused();
	SampleManager.PauseMissionMusicStream(manuallyPaused);
	if (!manuallyPaused && gTrackState == TRACK_FM && gStreamStarted &&
	    SampleManager.HasMissionMusicStreamFinished())
		SwitchToLoop();

	if (gTrackState == TRACK_SWITCHING) {
		if (gLoopStartDelay != 0) {
			gLoopStartDelay--;
			return;
		}
		gTrackState = TRACK_LOOP;
		StartTrack(VC_LOOP_PATH, true);
	}

	uint8 volume = MAX_VOLUME;
	if (gTrackState == TRACK_CLIMAX_FADING) {
		const uint32 elapsed = CTimer::GetTimeInMilliseconds() - gFadeStartTime;
		if (elapsed >= CLIMAX_FADE_OUT_TIME_MS) {
			SampleManager.StopMissionMusicStream();
			gTrackState = TRACK_CLIMAX_SILENT;
			gStreamStarted = false;
			return;
		}
		volume = (MAX_VOLUME * (CLIMAX_FADE_OUT_TIME_MS - elapsed)) /
		         CLIMAX_FADE_OUT_TIME_MS;
	} else if (gTrackState == TRACK_FADING) {
		const uint32 elapsed = CTimer::GetTimeInMilliseconds() - gFadeStartTime;
		if (elapsed >= FADE_OUT_TIME_MS) {
			SampleManager.StopMissionMusicStream();
			gTrackState = TRACK_STOPPED;
			gStreamStarted = false;
			return;
		}
		volume = (MAX_VOLUME * (FADE_OUT_TIME_MS - elapsed)) / FADE_OUT_TIME_MS;
	}
	if (gTrackState != TRACK_STOPPED)
		ApplyVolume(volume);
}

bool
IsFinaleActive()
{
	return gVCFinaleActive;
}

bool
IsRadioLocked()
{
	return Enabled && gVCFinaleActive;
}

void
DisplayTrackName()
{
	if (!Enabled) return;
	if (!gVCFinaleActive || gTrackNameFrames == 0 || CTimer::GetIsPaused() ||
	    TheCamera.m_WideScreenOn || CReplay::IsPlayingBack())
		return;
	gTrackNameFrames--;

	static wchar title[] = { '\'', 'S', 'e', 'l', 'f', ' ', 'C', 'o', 'n', 't', 'r', 'o', 'l', '\'', 0 };
	CFont::SetJustifyOff();
	CFont::SetBackgroundOff();
	CFont::SetScale(SCREEN_SCALE_X(0.8f), SCREEN_SCALE_Y(1.35f));
	CFont::SetPropOn();
	CFont::SetFontStyle(FONT_STANDARD);
	CFont::SetCentreOn();
	CFont::SetCentreSize(SCREEN_STRETCH_X(DEFAULT_SCREEN_WIDTH));
	CFont::SetColor(CRGBA(0, 0, 0, 255));
	CFont::PrintString(SCREEN_WIDTH / 2 + SCREEN_SCALE_X(2.0f),
		SCREEN_SCALE_Y(22.0f) + SCREEN_SCALE_Y(2.0f), title);
	CFont::SetColor(CRGBA(147, 196, 211, 255));
	CFont::PrintString(SCREEN_WIDTH / 2, SCREEN_SCALE_Y(22.0f), title);
	CFont::DrawFonts();
}
}
