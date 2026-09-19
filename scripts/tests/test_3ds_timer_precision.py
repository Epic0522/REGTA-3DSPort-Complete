#!/usr/bin/env python3
"""Exercise the real 3DS tick-to-double conversion against host arithmetic.

Regression test for: psTimer() reads svcGetSystemTick(), a 64-bit counter.
The ARM11 VFP cannot convert 64-bit integers to floating point, so the
conversion is hand-split into high and low 32-bit words (mirroring
common/libctru/source/os.c:13).  A dropped or mis-scaled high word would
still produce a plausible-looking clock, so pin the arithmetic exactly.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    # Column-zero closing brace; good enough for these simple functions.
    end = source.index('\n}', start) + 2
    return source[start:end]


def strip_comments(text):
    # osGetTime() is named in psTimer's explanatory comment, and the live
    # call is osGetTimeRef() which contains 'osGetTime' as a substring, so
    # the "no osGetTime" check has to look at code only, and at 'osGetTime('.
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


class TimerPrecision(unittest.TestCase):
    def test_tick_conversion_is_exact(self):
        source = (ROOT / 'stories/src/skel/3ds/3ds.cpp').read_text()
        convert = function(source, 'TicksToDouble(u64 value)')
        # Recover the 'static inline double' return type that precedes the
        # signature line, so the extracted fragment compiles standalone.
        convert = 'static inline double\n' + convert

        stub = r'''
#include <cassert>
#include <cstdint>
typedef uint64_t u64;
typedef uint32_t u32;
'''

        main = r'''
int main() {
 // Zero and the low word in isolation.
 assert(TicksToDouble(0ULL) == 0.0);
 assert(TicksToDouble(1ULL) == 1.0);
 assert(TicksToDouble(0xFFFFFFFFULL) == 4294967295.0);

 // The 32-bit boundary: the high word must contribute, scaled by 2^32.
 assert(TicksToDouble(0x100000000ULL) == 4294967296.0);
 assert(TicksToDouble(0x1FFFFFFFFULL) == 8589934591.0);

 // Both words populated.
 assert(TicksToDouble(0x123456789ABCDEFULL) == 81985529216486895.0);

 // ~30 days of uptime at the 268 MHz tick rate stays exact:
 // 30 * 24 * 3600 * 268111856 = 694945930752000, well inside 2^53.
 u64 month = 694945930752000ULL;
 assert(TicksToDouble(month) == 694945930752000.0);

 // A one-tick difference at that magnitude is still resolvable, which is
 // what CTimer's per-frame delta depends on.
 assert(TicksToDouble(month + 1ULL) - TicksToDouble(month) == 1.0);
}
'''

        with tempfile.TemporaryDirectory(prefix='regta-timer-test-') as tmp:
            path, binary = Path(tmp) / 'test.cpp', Path(tmp) / 'test'
            path.write_text(stub + convert + main)
            subprocess.run(['c++', '-std=c++11', '-fsanitize=address,undefined',
                            str(path), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_psTimer_does_not_use_osGetTime(self):
        # osGetTime() truncates to whole milliseconds; reintroducing it would
        # silently restore the timestep quantisation this change removed.
        source = (ROOT / 'stories/src/skel/3ds/3ds.cpp').read_text()
        body = strip_comments(function(source, 'psTimer(void)'))
        self.assertNotIn('osGetTime(', body)
        self.assertIn('svcGetSystemTick(', body)


if __name__ == '__main__':
    unittest.main()
