#!/usr/bin/env python3
"""Exercise the actual unload implementations with host resource stubs."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    # These definitions use column-zero braces; preprocessor alternatives may
    # contain two opening braces for one closing brace before preprocessing.
    end = source.index('\n}', start) + 2
    return source[start:end]


class PauseResources(unittest.TestCase):
    def test_resource_lifetime(self):
        stub = r'''
#include <cassert>
#include <cstdio>
#define _3DS
#define GAMEPAD_MENU
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))
int deletes, removes, musicChanges, placeUpdates;
enum { MENUPAGE_SOUND_SETTINGS, SOUND_FRONTEND_MENU_STARTING, MUSICMODE_GAME,
       MENUSPRITE_BACKGROUND = 0, MENUSPRITE_MAINMENU = 3,
       NUM_MENU_SPRITES = 30 };
bool gPauseBackgroundResident3DS;
const char *FrontendFilenames[28], *MenuFilenames[19];
struct Sprite { void Delete() { ++deletes; } };
struct Audio {
 void StopFrontEndTrack() {}
 void PlayFrontEndSound(int,int) {}
 void ChangeMusicMode(int) { ++musicChanges; }
} DMAudio;
struct Places { void ProcessAfterFrontEndShutDown() { ++placeUpdates; } };
struct CUserDisplay { static Places PlaceName; };
Places CUserDisplay::PlaceName;
struct CTxdStore {
 static int FindTxdSlot(const char*) { return 0; }
 static void RemoveTxd(int) { ++removes; }
};
struct CMenuManager {
 bool m_bSpritesLoaded = true, m_OnlySaveMenu = false;
 int m_nCurrScreen = 0;
 Sprite m_aFrontEndSprites[30], m_aMenuSprites[19];
 void UnloadTextures();
};
'''
        for game in ('III', 'miami', 'stories'):
            with self.subTest(game=game):
                source = (ROOT / game / 'src/core/Frontend.cpp').read_text()
                body = function(source, 'CMenuManager::UnloadTextures()')
                main = r'''
int main() {
 CMenuManager menu;
 // Only the exact pause-home background remains resident during gameplay.
 menu.UnloadTextures();
 assert(!menu.m_bSpritesLoaded);
 assert(gPauseBackgroundResident3DS);
 assert(deletes > 0 && removes > 0);
 int oldDeletes = deletes, oldRemoves = removes;
 menu.UnloadTextures();
 assert(deletes == oldDeletes && removes == oldRemoves);
}
'''
                with tempfile.TemporaryDirectory(prefix='regta-pause-test-') as tmp:
                    cpp, binary = Path(tmp) / 'test.cpp', Path(tmp) / 'test'
                    cpp.write_text(stub + '\nvoid\n' + body + main)
                    subprocess.run(['c++', '-std=c++11', '-fsanitize=address,undefined',
                                    str(cpp), '-o', str(binary)], check=True)
                    subprocess.run([str(binary)], check=True)

    def test_entry_contracts(self):
        cache = (ROOT / 'common/3ds/MenuTextureCache.h').read_text()
        # casepath() returns null when the spelling already matches the filesystem.
        # The exact source path must still participate in cache reads and writes.
        self.assertGreaterEqual(cache.count('resolved ? resolved : source'), 2)
        for game in ('III', 'miami', 'stories'):
            source = (ROOT / game / 'src/core/Frontend.cpp').read_text()
            controller = function(source, 'CMenuManager::LoadController(int8 type)')
            self.assertIn('#ifdef _3DS\n\treturn;\n#else', controller)
            load = function(source, 'CMenuManager::LoadAllTextures()')
            self.assertNotIn('MenuTexturesCached3DS', source)
            self.assertIn('LoadMenuTextureCache3DS(', load)
            self.assertIn('FinishMenuTextureCache3DS(', load)
            self.assertIn('if (CFont::ButtonsSlot == -1)', load)
            self.assertIn('resolved ? resolved : source', cache)
            self.assertIn('gPauseBackgroundResident3DS', source)
            self.assertIn('gPauseInputDelayFrames3DS = 2;', source)
            self.assertIn('ReleasePauseHomeTextures();', source)
            self.assertNotIn('CRGBA(18, 18, 18, 255)', source)
            if game != 'III':
                process = function(source, 'CMenuManager::Process(void)')
                self.assertIn('if (!m_bSpritesLoaded)\n\t\t\tLoadAllTextures();', process)
                self.assertIn('instantPauseEntry', source)
                self.assertIn('if (m_bGameNotLoaded || m_OnlySaveMenu)', source)
                self.assertIn('DrawStandardMenus(true);', source)
                self.assertIn('m_aFrontEndSprites[MENUSPRITE_BACKGROUND].Draw', source)
                switch = function(source, 'CMenuManager::SwitchToNewScreen(int8 screen)')
                for page in ('LOAD', 'DELETE', 'SAVE'):
                    self.assertIn('MENUPAGE_CHOOSE_' + page + '_SLOT', switch)
                self.assertIn('PcSaveHelper.PopulateSlotInfo();', switch)
                self.assertIn('if (m_OnlySaveMenu)\n#endif\n\t\tPcSaveHelper.PopulateSlotInfo();', source)
            else:
                process = function(source, 'CMenuManager::Process(void)')
                self.assertIn('const bool menuWasActive = m_bMenuActive;', process)
                self.assertIn('if (menuWasActive || m_bGameNotLoaded || m_bSaveMenuActive)', process)
                self.assertIn('m_aMenuSprites[MENUSPRITE_MAINMENU].Draw', source)

    def test_default_aspect_and_missing_frontend_text(self):
        for game in ('III', 'miami', 'stories'):
            frontend = (ROOT / game / 'src/core/Frontend.cpp').read_text()
            custom = (ROOT / game / 'src/core/MenuScreensCustom.cpp').read_text()
            self.assertIn('m_PrefsUseWideScreen = AR_5_4;', frontend)
            self.assertIn('m_PrefsUseWideScreen = AR_5_4;', custom)
        text = (ROOT / 'miami/src/text/Text.cpp').read_text()
        for key in ('FET_GFX', 'FEM_AUT', 'FEC_SLC', 'FED_LFL'):
            self.assertIn('{ "' + key + '"', text)
        text = (ROOT / 'stories/src/text/Text.cpp').read_text()
        for key in ('FEC_SLC', 'FED_LFL', 'FEC_CR3', 'FEC_LB3'):
            self.assertIn('{ "' + key + '"', text)

    def test_disk_cache(self):
        source = r'''
#include <cassert>
#include "common/3ds/MenuCacheIO.h"
using namespace MenuCacheIO;
void writeFile(const char *path, const void *data, size_t size) {
 FILE *f = fopen(path, "wb"); assert(f);
 assert(fwrite(data, 1, size, f) == size); assert(fclose(f) == 0);
}
int main() {
 const unsigned char payload[64] = {22, 0, 0, 0, 52};
 writeFile("source.txd", payload, sizeof(payload));
 Header identity; assert(Source("source.txd", identity));
 uint32_t size; assert(Read("cache", identity, size) == NULL);
 FILE *f = fopen("cache.tmp", "wb"); assert(f);
 fwrite(&identity, 1, sizeof(identity), f);
 fwrite(payload, 1, sizeof(payload), f); fclose(f);
 assert(Publish("cache.tmp", "cache", identity));
 assert(access("cache.tmp", F_OK) != 0);
 void *data = Read("cache", identity, size);
 assert(data && size == sizeof(payload) && memcmp(data, payload, size) == 0);
 free(data);
 // A source update invalidates the cache without touching either file.
 Header changed = identity; ++changed.sourceSize;
 assert(Read("cache", changed, size) == NULL);
 changed = identity; ++changed.sourceMtime;
 assert(Read("cache", changed, size) == NULL);
 // Version mismatch, truncation and byte corruption must be rejected.
 f = fopen("cache", "r+b"); assert(f);
 Header saved; fread(&saved, 1, sizeof(saved), f);
 saved.version++; rewind(f); fwrite(&saved, 1, sizeof(saved), f); fclose(f);
 assert(Read("cache", identity, size) == NULL);
 saved.version--;
 f = fopen("cache", "r+b"); fwrite(&saved, 1, sizeof(saved), f);
 fputc(0xff, f); fclose(f);
 assert(Read("cache", identity, size) == NULL);
 assert(truncate("cache", sizeof(Header) + 2) == 0);
 assert(Read("cache", identity, size) == NULL);
 // Interrupted temporary writes are never accepted/published.
 writeFile("cache.tmp", &identity, sizeof(identity));
 assert(!Publish("cache.tmp", "cache", identity));
 // The original asset remains byte-for-byte unchanged.
 unsigned char original[64]; f = fopen("source.txd", "rb");
 assert(fread(original, 1, sizeof(original), f) == sizeof(original)); fclose(f);
 assert(memcmp(original, payload, sizeof(payload)) == 0);
}
'''
        with tempfile.TemporaryDirectory(prefix='regta-menu-cache-test-') as tmp:
            cpp, binary = Path(tmp) / 'test.cpp', Path(tmp) / 'test'
            cpp.write_text(source)
            subprocess.run(['c++', '-std=c++11', '-fsanitize=address,undefined',
                            '-I', str(ROOT), str(cpp), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], cwd=tmp, check=True)


if __name__ == '__main__':
    unittest.main()
