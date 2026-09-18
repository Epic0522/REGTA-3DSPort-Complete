#!/usr/bin/env python3
"""Check README shell syntax and r55 preflight independently of SDK location."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BuildGuide(unittest.TestCase):
    def test_shell_examples(self):
        text = (ROOT / 'README.md').read_text()
        guide = text.split('### Linux and macOS:')[1].split('### Verify and compile')[0]
        for block in re.findall(r'```sh\n(.*?)```', guide, re.S):
            subprocess.run(['bash', '-n'], input=block, text=True, check=True)

    def test_game_guide_links(self):
        anchor = '#linux-and-macos-build-r55-from-official-sources'
        for game in ('III', 'miami', 'stories'):
            text = (ROOT / game / 'README.md').read_text()
            self.assertIn('../README.md' + anchor, text)
            self.assertNotIn('#ubuntu-build-r55', text)

    def test_compiler_guard(self):
        with tempfile.TemporaryDirectory(prefix='regta-sdk-guard-') as tmp:
            sdk = Path(tmp) / 'external-sdk'
            (sdk / 'bin').mkdir(parents=True)
            compiler = sdk / 'bin/arm-none-eabi-gcc'
            makefile = 'include ' + str(ROOT / 'common/devkitarm-r55.mk') + '\nall:;@true\n'
            for version, label, success in (
                ('10.2.0', 'arm-none-eabi-gcc (devkitARM release 55) 10.2.0', True),
                ('10.2.0', 'arm-none-eabi-gcc (GNU) 10.2.0', False),
                ('15.1.0', 'arm-none-eabi-gcc (devkitARM release 66) 15.1.0', False),
            ):
                compiler.write_text('#!/bin/sh\ncase "$1" in\n-dumpfullversion) echo "' +
                                    version + '";;\n*) echo "' + label + '";;\nesac\n')
                compiler.chmod(0o755)
                env = dict(os.environ, DEVKITARM=str(sdk))
                result = subprocess.run(['make', '-f', '-'], input=makefile, text=True,
                                        env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                self.assertEqual(result.returncode == 0, success, result.stderr)

    def test_clean_checkout_generates_git_sha1_source(self):
        for game in ('III', 'miami', 'stories'):
            makefile = (ROOT / game / 'build' / 'GNUmakefile').read_text()
            self.assertIn('GIT_SHA1_CPP\t:=\t../src/extras/GitSHA1.cpp', makefile)
            self.assertIn('$(filter-out $(GIT_SHA1_CPP),$(CPPFILES)) $(GIT_SHA1_CPP)',
                          makefile)
            self.assertIn('$(GIT_SHA1_CPP): force-git-sha1', makefile)
            self.assertIn('@bash ../printHash.sh $@', makefile)


if __name__ == '__main__':
    unittest.main()
