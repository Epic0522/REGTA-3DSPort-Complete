#!/usr/bin/env python3
"""Exercise the real CRunningScript::GetPadState() against host stubs.

Regression test for: the 3DS menu accept/cancel remap (Circle=confirm,
Cross=cancel) also applies to legacy shop scripts polling slots 15-17 while
player controls are disabled. That remap is wrong for mounted script prompts
(e.g. the Staunton Island Faggio time-trial kiosk), because Cross doubles as
the vehicle throttle: holding it into the pre-race countdown got read as
"cancel" and instantly failed the mission. Mounted prompts must keep the
vanilla PS2 slot assignments (cancel=Triangle, confirm=Cross) even while
controls are disabled for the countdown.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    end = source.index('\n}', start) + 2
    return source[start:end]


class ScriptPadSlots(unittest.TestCase):
    def _compile_and_run(self, main):
        source = (ROOT / 'stories/src/control/Script5.cpp').read_text()
        raw = function(source, 'int16 CRunningScript::GetPadState(uint16 pad, uint16 button)')
        raw = raw.replace('CRunningScript::', '')

        stub = r'''
#include <cassert>
#define _3DS
struct State {
 int LeftStickX = 0, LeftStickY = 0, RightStickX = 0, RightStickY = 0;
 int LeftShoulder1 = 0, LeftShoulder2 = 0, RightShoulder1 = 0, RightShoulder2 = 0;
 int DPadUp = 0, DPadDown = 0, DPadLeft = 0, DPadRight = 0;
 int Start = 0, Select = 0;
 int Square = 0, Triangle = 0, Cross = 0, Circle = 0;
 int LeftShock = 0, RightShock = 0;
};
struct CPad {
 State NewState;
 bool disabled = false;
 bool mounted = false;
 bool ArePlayerControlsDisabled() { return disabled; }
 static bool ScriptPromptIsMounted();
 static CPad *GetPad(int pad) { static CPad instance; return &instance; }
};
bool gMounted = false;
bool CPad::ScriptPromptIsMounted() { return gMounted; }
typedef unsigned short uint16;
typedef short int16;

'''
        cpp = stub + raw + '\n'
        with tempfile.TemporaryDirectory(prefix='regta-scriptpad-test-') as tmp:
            path, binary = Path(tmp) / 'test.cpp', Path(tmp) / 'test'
            path.write_text(cpp + main)
            subprocess.run(['c++', '-std=c++11', '-fsanitize=address,undefined',
                             str(path), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_shop_menu_uses_3ds_remap_when_disabled(self):
        # On-foot shop menu (Ammu-Nation, clothes): controls disabled while
        # the menu is open, not mounted. Slots must follow the 3DS remap.
        main = r'''
int main() {
 CPad *pad = CPad::GetPad(0);
 gMounted = false;
 pad->disabled = true;
 assert(GetPadState(0, 15) == pad->NewState.Cross);   // remapped cancel
 assert(GetPadState(0, 16) == pad->NewState.Circle);  // remapped confirm
 assert(GetPadState(0, 17) == pad->NewState.Triangle);
}
'''
        self._compile_and_run(main)

    def test_shop_menu_uses_vanilla_slots_when_enabled(self):
        main = r'''
int main() {
 CPad *pad = CPad::GetPad(0);
 gMounted = false;
 pad->disabled = false;
 assert(GetPadState(0, 15) == pad->NewState.Triangle);
 assert(GetPadState(0, 16) == pad->NewState.Cross);
 assert(GetPadState(0, 17) == pad->NewState.Circle);
}
'''
        self._compile_and_run(main)

    def test_mounted_prompt_keeps_vanilla_slots_even_when_disabled(self):
        # Regression: the Faggio time-trial kiosk during the countdown.
        # Controls are disabled AND the player is mounted; cancel must stay
        # on Triangle (never a driving input), not Cross (the throttle).
        main = r'''
int main() {
 CPad *pad = CPad::GetPad(0);
 gMounted = true;
 pad->disabled = true;
 pad->NewState.Cross = 1; // player is holding throttle into the countdown
 assert(GetPadState(0, 15) == pad->NewState.Triangle);
 assert(GetPadState(0, 16) == pad->NewState.Cross);
 assert(GetPadState(0, 17) == pad->NewState.Circle);
}
'''
        self._compile_and_run(main)

    def test_mounted_prompt_matches_vanilla_when_enabled(self):
        main = r'''
int main() {
 CPad *pad = CPad::GetPad(0);
 gMounted = true;
 pad->disabled = false;
 assert(GetPadState(0, 15) == pad->NewState.Triangle);
 assert(GetPadState(0, 16) == pad->NewState.Cross);
 assert(GetPadState(0, 17) == pad->NewState.Circle);
}
'''
        self._compile_and_run(main)


if __name__ == '__main__':
    unittest.main()
