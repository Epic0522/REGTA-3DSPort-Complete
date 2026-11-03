#!/usr/bin/env python3
"""Install (or remove) the bundled Chinese text pack for Vice City.

The pack ships the text table plus the pre-baked glyph atlas:

    miami/tools/chs/TEXT/american.gxt      Chinese text table (UTF-16LE GXT)
    miami/tools/chs/models/chsfont.a8      glyph coverage atlas (1024x1024, 8-bit)
    miami/tools/chs/models/chsfont.map     code point -> atlas slot table

Installing copies them into the curated override tree (gamefiles/revc) and
regenerates common/3ds/InstallManifest.h.  That manifest pins the SHA-256 of
every curated file, so editing the text table without refreshing it makes
RegtaInstall::validate() abort at startup.

Usage:
    python3 miami/tools/apply_chs_text.py                # install the pack
    python3 miami/tools/apply_chs_text.py --remove       # back to English
    python3 miami/tools/apply_chs_text.py --gxt <file>   # install another table
"""
import argparse
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
PACK = ROOT / 'miami' / 'tools' / 'chs'
OVERRIDES = ROOT / 'gamefiles' / 'revc'
MANIFEST = ROOT / 'scripts' / 'tools' / 'generate_install_manifest.py'
FILES = ('TEXT/american.gxt', 'models/chsfont.a8', 'models/chsfont.map')


def refresh_manifest():
    subprocess.run([sys.executable, str(MANIFEST)], check=True, cwd=ROOT)


def remove():
    subprocess.run(['git', 'checkout', '--', str(OVERRIDES.relative_to(ROOT))],
                   cwd=ROOT, check=True)
    pack_models = OVERRIDES / 'models'
    for path in FILES:
        if path.startswith('models/'):
            target = OVERRIDES / path
            if target.is_file():
                target.unlink()
    if pack_models.is_dir() and not any(pack_models.iterdir()):
        pack_models.rmdir()
    refresh_manifest()
    print('removed the Chinese text pack; rebuild to restore English text')


def install(gxt):
    for relative, source in zip(FILES, (gxt, PACK / 'models' / 'chsfont.a8',
                                        PACK / 'models' / 'chsfont.map')):
        if source is None or not source.is_file():
            raise SystemExit('missing pack file: %s' % source)
        target = OVERRIDES / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        print('%-24s %8d bytes' % (relative, target.stat().st_size))
    refresh_manifest()
    print('\nrebuild with scripts/build.sh revc, then run setup-game.sh again')


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--gxt', type=pathlib.Path,
                        help='install this GXT instead of the bundled one')
    parser.add_argument('--remove', action='store_true',
                        help='remove the pack and restore the English table')
    args = parser.parse_args()

    if args.remove:
        remove()
    else:
        install(args.gxt or PACK / 'TEXT' / 'american.gxt')


if __name__ == '__main__':
    main()
