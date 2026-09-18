#!/usr/bin/env python3
"""Host LOD regressions and menu checks using the real 3DS preprocessor flags."""
import os
import re
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class WorldAndMenus(unittest.TestCase):
    def test_world_distance(self):
        source = r'''
#include <cassert>
#include <cmath>
#include "common/3ds/WorldDrawDistance.h"
using namespace WorldDrawDistance3DS;
int main() {
    assert(Scale("office", false, false) == 0.65f);
    assert(Scale("streetlamp", false, true) == 0.75f);
    assert(Scale("VEG_PALM01", false, false) == 1.2f);
    assert(Scale("tree_shadow", false, false) == 0.65f);
    assert(Scale("IslandLODmainland", false, false) == 1.0f);
    assert(SurfaceDistance(400, 110, 100, true, false) == 10);
    assert(SurfaceDistance(400, 90, 100, true, false) == 0);
    assert(SurfaceDistance(2000, 110, 2000, true, true) == 2000);
    assert(SurfaceDistance(400, 110, 100, false, false) == 400);
    assert(SurfaceDistance(400, 10, 15, true, false) == 400);
    // A close wall stays opaque even when the building origin is far away.
    float lod = SurfaceDistance(400, 110, 100, true, false) / 0.65f;
    assert(lod < 100);
    // Standard 20-unit fade is monotonic in the same distance used for LOD.
    float previous = 1;
    for(int d=65; d<=78; ++d) {
        float alpha = fmaxf(0, fminf(1, (100 - (d / 0.65f - 20)) / 20));
        assert(alpha <= previous + 0.00001f);
        previous = alpha;
    }
}
'''
        with tempfile.TemporaryDirectory(prefix="regta-lod-test-") as directory:
            cpp = Path(directory) / "test.cpp"
            binary = Path(directory) / "test"
            cpp.write_text(source)
            subprocess.run(["c++", "-std=c++11", "-fsanitize=address,undefined",
                            "-I", str(ROOT), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_compiled_menus(self):
        env = os.environ.copy()
        arm = env.get("DEVKITARM", str(ROOT / "toolchains/devkitARM-r55"))
        env.setdefault("DEVKITPRO", str(ROOT / "toolchains"))
        env["DEVKITARM"] = arm
        env["PATH"] = arm + "/bin:" + env["PATH"]
        for game in ("III", "miami", "stories"):
            with self.subTest(game=game):
                directory = ROOT / game / "build"
                source_name = "MenuScreensCustom.cpp"
                dry = subprocess.check_output([
                    "make", "-n", "-f", "GNUmakefile", "-W", "../src/core/" + source_name,
                    "LOADING_PIPELINE=1", "BOTTOM_LOADING=1", "BOTTOM_RADAR=1"],
                    cwd=directory, env=env, text=True)
                command = next(line for line in dry.splitlines()
                               if line.startswith("arm-none-eabi-g++ ") and
                               "-c ../src/core/" + source_name + " " in line)
                tokens = shlex.split(command)
                args = []
                skip = False
                for token in tokens:
                    if skip:
                        skip = False
                    elif token in ("-o", "-MF"):
                        skip = True
                    elif token not in ("-c", "-MMD", "-MP"):
                        args.append(token)
                args += ["-E", "-P"]
                output = subprocess.check_output(args, cwd=directory, env=env, text=True)
                menu = output[re.search(r'CMenuScreen(?:Custom)? aScreens\[[^\]]*\]\s*=', output).start():]
                for key in ("FEM_LOD", "FEC_TYP", "FEC_VIB", "FED_RES", "FEM_SCF",
                            "FED_AAS", "FEC_JOD", "FEC_MOU", "FEC_RED", "FET_AMS", "FEA_3DH",
                            "FEA_SPK", "FET_DAM", "FEA_MPB", "FED_MBL", "FEM_2PR"):
                    self.assertNotRegex(menu, r'MENUACTION_\w+\s*,\s*"' + key + '"')
                for key in ("FEA_MUS", "FEA_SFX", "FED_SUB", "FEM_FRM"):
                    self.assertIn('"' + key + '"', menu)
                if game != "stories":
                    self.assertIn("MENUACTION_CTRLMETHOD", menu)
                print(game + ": compiled 3DS menu checks passed")


if __name__ == "__main__":
    unittest.main()
