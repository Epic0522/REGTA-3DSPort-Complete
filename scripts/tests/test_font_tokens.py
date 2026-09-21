#!/usr/bin/env python3
"""Exercise the real CFont::ParseToken against host stubs.

Regression test for: garbled R3/L3 help text (e.g. the Taxi Driver side
mission's "Press ~k~ ~TGSUB~" cancel prompt). The 3DS touch-instruction
rewrite in CMessages::Normalize3DSTouchInstructionPrefix (text/Messages.cpp)
produces adjacent tokens like "~w~~h~TOUCH, THEN TAP R3 ...". LCS dropped
reVC's adjacent-token recursion in ParseToken, so CFont::RenderFontBuffer
printed the next token's opening '~' as a literal glyph and desynced the
parse, misfiring a button-icon sprite from the following text. See
stories/src/renderer/Font.cpp, CFont::ParseToken.
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


class FontTokenParsing(unittest.TestCase):
    def _build(self, fix_bugs):
        source = (ROOT / 'stories/src/renderer/Font.cpp').read_text()
        parse_token = function(
            source, 'wchar*\nCFont::ParseToken(wchar* str, CRGBA &color, bool &flash, bool &bold)')
        body = parse_token.replace('CFont::ParseToken', 'ParseToken')

        stub = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
typedef uint16_t wchar;
#define BUTTON_ICONS
''' + ('#define FIX_BUGS\n' if fix_bugs else '') + r'''
enum {
    BUTTON_NONE = -1, BUTTON_UP, BUTTON_DOWN, BUTTON_LEFT, BUTTON_RIGHT,
    BUTTON_CROSS, BUTTON_CIRCLE, BUTTON_SQUARE, BUTTON_TRIANGLE,
    BUTTON_L1, BUTTON_L2, BUTTON_L3, BUTTON_R1, BUTTON_R2, BUTTON_R3,
    BUTTON_RSTICK_UP, BUTTON_RSTICK_DOWN, BUTTON_RSTICK_LEFT, BUTTON_RSTICK_RIGHT,
    MAX_BUTTON_ICONS
};
struct CRGBA { unsigned char r = 0, g = 0, b = 0, a = 255; };
struct CFontDetails { CRGBA color; bool anonymous_23 = false; };
struct CFont {
    CFontDetails Details;
    int PS2Symbol = BUTTON_NONE;
'''
        cpp = stub + body + '\n};\n'
        return cpp

    def _compile_and_run(self, cpp, main):
        with tempfile.TemporaryDirectory(prefix='regta-font-test-') as tmp:
            path, binary = Path(tmp) / 'test.cpp', Path(tmp) / 'test'
            path.write_text(cpp + main)
            subprocess.run(['c++', '-std=c++11', '-fsanitize=address,undefined',
                             str(path), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_adjacent_color_tokens_consumed_without_literal_tilde(self):
        # "~w~~h~abc" - two adjacent pure-color tokens then real text.
        # With the fix, ParseToken must skip BOTH tokens in one call and
        # land on 'a', not on the second token's opening '~'.
        main = r'''
int main() {
    CFont font;
    font.Details.color.r = 175; font.Details.color.g = 175; font.Details.color.b = 175;
    CRGBA color = font.Details.color;
    bool flash = false, bold = false;
    wchar str[] = { '~','w','~','~','h','~','a','b','c','\0' };
    wchar *s = font.ParseToken(str, color, flash, bold);
    assert(*s == 'a');
    assert(color.r == 255 && color.g == 255 && color.b == 255); // ~h~ applied
    assert(font.PS2Symbol == BUTTON_NONE);
}
'''
        self._compile_and_run(self._build(fix_bugs=True), main)

    def test_adjacent_token_then_button_icon_fires_correctly(self):
        # "~w~~T~abc" mirrors the real garble: a color token immediately
        # followed by a button-icon token (the 'T' in "TOUCH..." was
        # misparsed as a literal ~T~ triangle-icon token before the fix).
        main = r'''
int main() {
    CFont font;
    font.Details.color.r = 175; font.Details.color.g = 175; font.Details.color.b = 175;
    CRGBA color = font.Details.color;
    bool flash = false, bold = false;
    wchar str[] = { '~','w','~','~','T','~','a','b','c','\0' };
    wchar *s = font.ParseToken(str, color, flash, bold);
    assert(*s == 'a');
    assert(font.PS2Symbol == BUTTON_TRIANGLE);
}
'''
        self._compile_and_run(self._build(fix_bugs=True), main)

    def test_without_fix_bugs_desyncs_on_adjacent_tokens(self):
        # Documents the pre-fix (FIX_BUGS undefined) behavior: ParseToken
        # returns a pointer to the second token's opening '~' instead of
        # skipping past it, which is what causes RenderFontBuffer to print
        # a literal '~' and misfire a button icon from the following text.
        main = r'''
int main() {
    CFont font;
    font.Details.color.r = 175; font.Details.color.g = 175; font.Details.color.b = 175;
    CRGBA color = font.Details.color;
    bool flash = false, bold = false;
    wchar str[] = { '~','w','~','~','h','~','a','b','c','\0' };
    wchar *s = font.ParseToken(str, color, flash, bold);
    assert(*s == '~'); // desync: lands on the second token's delimiter, not 'a'
}
'''
        self._compile_and_run(self._build(fix_bugs=False), main)


if __name__ == '__main__':
    unittest.main()
