#include "common.h"

#include "FinalMissionMusic.h"

#include "Camera.h"
#include "CutsceneMgr.h"
#include "Font.h"
#include "MusicManager.h"
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
		TRACK_LOOP,
		TRACK_FADING
	};

	static const char *GTA3_FM_PATH = "audio\\music\\PUSH_FM.WAV";
	static const char *GTA3_LOOP_PATH = "audio\\music\\PUSH_LOOP.WAV";
	static const uint32 FADE_OUT_TIME_MS = 1000;

	bool gGTA3FinaleActive;
	eTrackState gTrackState = TRACK_STOPPED;
	uint32 gFadeStartTime;
	bool gStreamStarted;
	bool gWasInCar;
	uint8 gTrackNameFrames;

	bool
	StartTrack(const char *path, bool loop)
	{
		SampleManager.StopMissionMusicStream();
		gStreamStarted = Enabled && SampleManager.StartMissionMusicStream(path, loop);
		return gStreamStarted;
	}

	void
	SwitchToLoop()
	{
		if (!gGTA3FinaleActive || gTrackState != TRACK_FM)
			return;
		gTrackState = TRACK_LOOP;
		StartTrack(GTA3_LOOP_PATH, true);
	}

	void
	ApplyVolume(uint8 volume)
	{
		/* The mastered finale track is louder than GTA III's native mix. Keep
		 * gameplay at 80%, then apply the existing half-volume cutscene duck. */
		volume = (uint32(volume) * 4) / 5;
		if (CCutsceneMgr::IsRunning() || TheCamera.m_WideScreenOn)
			volume /= 2;
		SampleManager.SetMissionMusicStreamVolume(volume);
	}
}

void
BeginGTA3Finale()
{
	EndGTA3Finale();
	gGTA3FinaleActive = true;
	gTrackState = TRACK_FM;
	gWasInCar = false;
	gTrackNameFrames = 0;
	StartTrack(GTA3_FM_PATH, false);
}

void
FadeOutGTA3Finale()
{
	if (!gGTA3FinaleActive || gTrackState == TRACK_STOPPED || gTrackState == TRACK_FADING)
		return;
	gTrackState = TRACK_FADING;
	gFadeStartTime = CTimer::GetTimeInMilliseconds();
}

void
EndGTA3Finale()
{
	if (gGTA3FinaleActive || gStreamStarted)
		SampleManager.StopMissionMusicStream();
	gGTA3FinaleActive = false;
	gTrackState = TRACK_STOPPED;
	gFadeStartTime = 0;
	gStreamStarted = false;
	gWasInCar = false;
	gTrackNameFrames = 0;
}

void
Update()
{
	if (!gGTA3FinaleActive)
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
		if (!gStreamStarted && gTrackState == TRACK_FM) StartTrack(GTA3_FM_PATH, false);
		else if (!gStreamStarted && gTrackState == TRACK_LOOP) StartTrack(GTA3_LOOP_PATH, true);
	}
	const bool manuallyPaused = CTimer::GetIsUserPaused();
	SampleManager.PauseMissionMusicStream(manuallyPaused);
	if (!manuallyPaused && gTrackState == TRACK_FM && gStreamStarted &&
	    SampleManager.HasMissionMusicStreamFinished())
		SwitchToLoop();

	uint8 volume = MAX_VOLUME;
	if (gTrackState == TRACK_FADING) {
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
	return gGTA3FinaleActive;
}

bool
IsRadioLocked()
{
	return Enabled && gGTA3FinaleActive;
}

void
DisplayTrackName()
{
	if (!Enabled) return;
	if (!gGTA3FinaleActive)
		return;

	const bool inCar = MusicManager.PlayerInCar();
	if (CTimer::GetIsPaused() || TheCamera.m_WideScreenOn ||
	    CReplay::IsPlayingBack() || !inCar) {
		gWasInCar = false;
		gTrackNameFrames = 0;
		return;
	}
	if (!gWasInCar) {
		gWasInCar = true;
		gTrackNameFrames = 60;
	} else if (gTrackNameFrames != 0) {
		gTrackNameFrames--;
	}
	if (gTrackNameFrames == 0)
		return;

	/* GTA III's heading atlas uses an upright uppercase row and an italic
	 * lowercase row.  Keep the song title in the latter so its first glyph
	 * does not visibly break the radio-title style. */
	static wchar title[] = { '\'', 'p', 'u', 's', 'h', ' ', 'i', 't', ' ', 't', 'o', ' ',
		't', 'h', 'e', ' ', 'l', 'i', 'm', 'i', 't', '\'', 0 };
	CFont::SetJustifyOff();
	CFont::SetBackgroundOff();
	CFont::SetScale(SCREEN_SCALE_X(0.8f), SCREEN_SCALE_Y(1.35f));
	CFont::SetPropOn();
	CFont::SetFontStyle(FONT_HEADING);
	CFont::SetCentreOn();
	CFont::SetCentreSize(SCREEN_SCALE_X(DEFAULT_SCREEN_WIDTH));
	CFont::SetColor(CRGBA(0, 0, 0, 255));
	CFont::PrintString(SCREEN_WIDTH / 2 + SCREEN_SCALE_X(2.0f),
		SCREEN_SCALE_Y(22.0f) + SCREEN_SCALE_Y(2.0f), title);
	CFont::SetColor(CRGBA(147, 196, 211, 255));
	CFont::PrintString(SCREEN_WIDTH / 2, SCREEN_SCALE_Y(22.0f), title);
	CFont::DrawFonts();
}
}
