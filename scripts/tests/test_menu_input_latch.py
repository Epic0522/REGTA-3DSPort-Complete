#!/usr/bin/env python3
"""Exercise the real 3DS weapon-suppression latch against host stubs.

Regression test for: menu accept (Circle/A) is also the weapon-fire button.
A press held while a script menu (e.g. the clothes shop) disables player
controls must not fire the weapon the instant controls are re-enabled.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    # Column-zero closing brace; good enough for these simple functions.
    end = source.index('\n}', start) + 2
    return source[start:end]


class MenuInputLatch(unittest.TestCase):
    def test_weapon_suppressed_across_menu_close(self):
        source = (ROOT / 'stories/src/core/Pad.cpp').read_text()
        raw = function(source, 'int32 CPad::GetWeaponButtonRaw(void)')
        get_weapon = function(source, 'int32 CPad::GetWeapon(void)')
        update = function(source, 'void CPad::UpdateWeaponSuppression(void)')

        stub = r'''
#include <cassert>
#define _3DS
struct State { bool Circle = false, Cross = false, RightShoulder1 = false; };
struct CPad {
 int Mode = 0;
 bool IsAffectedByController = true;
 bool disabled = false;
 bool bSuppressWeaponUntilRelease = false;
 State NewState, OldState;
 bool ArePlayerControlsDisabled() { return disabled; }
'''
        stub = stub.replace('int32', 'int').replace('bool CPad::', 'bool CPad::')
        # CURMODE reads Mode directly for this test; controller-switch detail
        # is irrelevant to the suppression latch under test.
        cpp = (stub +
               '#define CURMODE (Mode)\n' +
               raw.replace('int32 CPad::', 'int ').replace('CPad::', '') +
               '\n' +
               get_weapon.replace('int32 CPad::', 'int ').replace('CPad::', '') +
               '\n' +
               update.replace('void CPad::', 'void ').replace('CPad::', '') +
               '\n};\n')

        main = r'''
int main() {
 CPad pad;
 pad.Mode = 0; // Circle slot

 // Player opens a menu (e.g. clothes shop) while holding A/Circle.
 pad.disabled = true;
 pad.NewState.Circle = true;
 pad.UpdateWeaponSuppression();
 assert(pad.bSuppressWeaponUntilRelease);

 // Controls re-enable next frame; button is still physically held.
 pad.disabled = false;
 assert(!pad.GetWeapon());

 // Latch must stay armed across several frames while still held.
 pad.UpdateWeaponSuppression();
 assert(pad.bSuppressWeaponUntilRelease);
 assert(!pad.GetWeapon());
 pad.UpdateWeaponSuppression();
 assert(!pad.GetWeapon());

 // Physical release clears the latch.
 pad.NewState.Circle = false;
 pad.UpdateWeaponSuppression();
 assert(!pad.bSuppressWeaponUntilRelease);

 // A fresh press after release fires normally.
 pad.NewState.Circle = true;
 assert(pad.GetWeapon());
}
'''
        with tempfile.TemporaryDirectory(prefix='regta-latch-test-') as tmp:
            path, binary = Path(tmp) / 'test.cpp', Path(tmp) / 'test'
            path.write_text(cpp + main)
            subprocess.run(['c++', '-std=c++11', '-fsanitize=address,undefined',
                             str(path), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_unaffected_when_never_disabled(self):
        # Ordinary gameplay firing (never went through a disabled-controls
        # window) must be completely unaffected by the latch.
        source = (ROOT / 'stories/src/core/Pad.cpp').read_text()
        raw = function(source, 'int32 CPad::GetWeaponButtonRaw(void)')
        get_weapon = function(source, 'int32 CPad::GetWeapon(void)')
        update = function(source, 'void CPad::UpdateWeaponSuppression(void)')

        stub = r'''
#include <cassert>
#define _3DS
struct State { bool Circle = false, Cross = false, RightShoulder1 = false; };
struct CPad {
 int Mode = 0;
 bool IsAffectedByController = true;
 bool disabled = false;
 bool bSuppressWeaponUntilRelease = false;
 State NewState, OldState;
 bool ArePlayerControlsDisabled() { return disabled; }
'''
        cpp = (stub +
               '#define CURMODE (Mode)\n' +
               raw.replace('int32 CPad::', 'int ').replace('CPad::', '') +
               '\n' +
               get_weapon.replace('int32 CPad::', 'int ').replace('CPad::', '') +
               '\n' +
               update.replace('void CPad::', 'void ').replace('CPad::', '') +
               '\n};\n')

        main = r'''
int main() {
 CPad pad;
 pad.UpdateWeaponSuppression();
 pad.NewState.Circle = true;
 assert(pad.GetWeapon());
 pad.NewState.Circle = false;
 assert(!pad.GetWeapon());
}
'''
        with tempfile.TemporaryDirectory(prefix='regta-latch-test-') as tmp:
            path, binary = Path(tmp) / 'test.cpp', Path(tmp) / 'test'
            path.write_text(cpp + main)
            subprocess.run(['c++', '-std=c++11', '-fsanitize=address,undefined',
                             str(path), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
