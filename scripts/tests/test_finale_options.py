#!/usr/bin/env python3
"""Host-test the actual finale stream gate and update code with audio spies."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def body(source, name):
    start = source.index(name)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class FinaleOptions(unittest.TestCase):
    def test_real_audio_gate(self):
        for game in ('III', 'miami', 'stories'):
            with self.subTest(game=game):
                source = (ROOT / game / 'src/audio/FinalMissionMusic.cpp').read_text()
                self.assertIn('int8 Enabled = 1;', source)
                stub = r'''
#include <cassert>
#include <cstdint>
using uint8 = uint8_t; using uint32 = uint32_t; using int8 = int8_t;
int starts, stops, repairs; bool lastLoop;
int8 Enabled = 1;
enum { TRACK_STOPPED, TRACK_FM, TRACK_LOOP, TRACK_SWITCHING,
       TRACK_FADING, TRACK_CLIMAX_FADING, TRACK_CLIMAX_SILENT };
int gTrackState = TRACK_FM;
bool gGTA3FinaleActive = true, gVCFinaleActive = true, gLCSFinaleActive = true;
bool gStreamStarted, gBoatChaseActive = true, gLighthouseTransition;
uint32 gFadeStartTime; uint8 gLoopStartDelay;
const uint32 FADE_OUT_TIME_MS = 1000, CLIMAX_FADE_OUT_TIME_MS = 1500;
const uint8 MAX_VOLUME = 127;
const char *GTA3_FM_PATH = "fm", *GTA3_LOOP_PATH = "loop";
const char *VC_FM_PATH = "fm", *VC_LOOP_PATH = "loop";
const char *LCS_FM_PATH = "fm", *LCS_LOOP_PATH = "loop";
struct Audio {
 void StopMissionMusicStream() { ++stops; }
 bool StartMissionMusicStream(const char *, bool loop) { ++starts; lastLoop=loop; return true; }
 void PauseMissionMusicStream(bool) {}
 bool HasMissionMusicStreamFinished() { return false; }
} SampleManager;
struct CTimer {
 static bool GetIsUserPaused() { return false; }
 static uint32 GetTimeInMilliseconds() { return 0; }
};
void ApplyVolume(uint8) {}
void SwitchToLoop() {}
void SwitchLCSFinaleToLoop() {}
void RememberSalvatoreChaseBoat() { ++repairs; }
void SetLighthouseGunnersOnPlayer() { ++repairs; }
'''
                actual = '\nbool\n' + body(source, 'StartTrack(const char *path, bool loop)')
                actual += '\nvoid\n' + body(source, 'Update()')
                actual += '\nbool\n' + body(source, 'IsRadioLocked()')
                actual += '\nbool\n' + body(source, 'IsFinaleActive()')
                main = r'''
int main() {
 assert(IsRadioLocked());
 Enabled=0;
 assert(!StartTrack("fm", false) && starts==0);
 assert(!IsRadioLocked());
 assert(IsFinaleActive()); // OFF cannot suppress mission-end/failure cleanup.
 Update();
 Enabled=1; Update();
 assert(starts==1 && !lastLoop);
 Update(); assert(starts==1);
 Enabled=0; int previousStops=stops; Update();
 assert(stops==previousStops+1 && !gStreamStarted && !IsRadioLocked());
 // A phase transition while OFF tracks LOOP, without opening a stream.
 gTrackState=TRACK_LOOP; StartTrack("loop", true);
 assert(starts==1);
 Update(); Enabled=1; Update();
 assert(starts==2 && lastLoop && IsRadioLocked());
 Update(); assert(starts==2);
'''
                if game == 'stories':
                    main += 'assert(repairs >= 10);\n'
                main += '}\n'
                with tempfile.TemporaryDirectory(prefix='regta-finale-option-') as tmp:
                    cpp, binary = Path(tmp) / 'test.cpp', Path(tmp) / 'test'
                    cpp.write_text(stub + actual + main)
                    subprocess.run(['c++', '-std=c++11', '-fsanitize=address,undefined',
                                    str(cpp), '-o', str(binary)], check=True)
                    subprocess.run([str(binary)], check=True)

    def test_menu_persistence_and_radio_paths(self):
        for game in ('III', 'miami', 'stories'):
            re3 = (ROOT / game / 'src/core/re3.cpp').read_text()
            self.assertIn('&FinalMissionMusic::Enabled, false, nil, "Audio", "FinalMissionBGM"', re3)
            self.assertIn('option.m_CFO->save', re3)  # existing generic INI persistence
            frontend = (ROOT / game / 'src/core/Frontend.cpp').read_text()
            self.assertIn('m_ControlMethod = CONTROL_CLASSIC;', frontend)
            music = (ROOT / game / 'src/audio/MusicManager.cpp').read_text()
            for function in ('ServiceGameMode()', 'DisplayRadioStationName()', 'ChangeRadioChannel()'):
                self.assertIn('FinalMissionMusic::IsRadioLocked()', body(music, 'cMusicManager::' + function))
            self.assertIn('pCurrentStation = nil;\n\t\tcDisplay = 0;', music)
            if game != 'stories':
                script = (ROOT / game / 'src/control/Script.cpp').read_text()
                self.assertNotIn('FinalMissionMusic::IsRadioLocked()', script)
                self.assertIn('m_bMissionFlag && FinalMissionMusic::IsFinaleActive()', script)


if __name__ == '__main__':
    unittest.main()
