import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
MESSAGES_H = REPO_ROOT / "stories" / "src" / "text" / "Messages.h"
HUD_H = REPO_ROOT / "stories" / "src" / "renderer" / "Hud.h"
HUD_CPP = REPO_ROOT / "stories" / "src" / "renderer" / "Hud.cpp"


def _num_big_messages():
    text = MESSAGES_H.read_text()
    m = re.search(r"#define\s+NUMBIGMESSAGES\s+(\d+)", text)
    assert m, "NUMBIGMESSAGES not found in Messages.h"
    return int(m.group(1))


def _array_extent(text, array_name):
    m = re.search(re.escape(array_name) + r"\[(\d+)\]", text)
    assert m, f"{array_name}[N] not found"
    return int(m.group(1))


class TestHudBigMessageSlots(unittest.TestCase):
    def setUp(self):
        self.n = _num_big_messages()
        self.hud_h = HUD_H.read_text()
        self.hud_cpp = HUD_CPP.read_text()

    def test_storage_arrays_match_numbigmessages(self):
        for name in ("m_BigMessage", "BigMessageDuration"):
            self.assertEqual(
                _array_extent(self.hud_h, name), self.n,
                f"{name} in Hud.h must be sized [NUMBIGMESSAGES]",
            )

    def test_state_arrays_match_numbigmessages(self):
        for name in ("BigMessageInUse", "BigMessageAlpha", "BigMessageX"):
            self.assertEqual(
                _array_extent(self.hud_h, name), self.n,
                f"{name} in Hud.h must be sized [NUMBIGMESSAGES]",
            )
            self.assertEqual(
                _array_extent(self.hud_cpp, name), self.n,
                f"{name} definition in Hud.cpp must be sized [NUMBIGMESSAGES]",
            )

    def test_clear_loop_bound_matches_numbigmessages(self):
        # GetRidOfAllHudMessages() clears BigMessageInUse[i] in a loop;
        # its upper bound must equal NUMBIGMESSAGES or every slot leaks
        # stale text across mission/save boundaries.
        m = re.search(
            r"for\s*\(\s*int\s+i\s*=\s*0\s*;\s*i\s*<\s*(\d+)\s*;\s*i\+\+\s*\)\s*\{\s*"
            r"BigMessageInUse\[i\]\s*=\s*0\.0f;",
            self.hud_cpp,
        )
        self.assertIsNotNone(m, "BigMessageInUse clear loop not found in Hud.cpp")
        self.assertEqual(int(m.group(1)), self.n)

    def test_slot_7_is_drawn(self):
        self.assertIn(
            "m_BigMessage[7]", self.hud_cpp,
            "Hud.cpp must contain a draw reference to m_BigMessage[7] "
            "(print_big style 8) or the fail-reason text for every "
            "mission in the game is silently dropped",
        )


if __name__ == "__main__":
    unittest.main()
