#!/usr/bin/env python3
"""Host-side MVD material control-flow tests; not hardware conversion tests."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class MvdMaterial(unittest.TestCase):
    def test_failure_cleanup_and_material_output(self):
        source = r'''
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <cstring>
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using Result = int32_t;
#define R_FAILED(r) ((r) < 0)
#define R_SUCCEEDED(r) ((r) >= 0)
enum { MVDMODE_COLORFORMATCONV, MVD_INPUT_YUYV422, MVD_OUTPUT_BGR565 };
const int MVD_STATUS_OK = 0x17000;
struct MVDSTD_Config { u16 *out; u8 *in; unsigned pixels; };
static int scenario, allocated, allocCalls, initCalls, exitCalls, flushCalls;
Result APT_CheckNew3DS(bool *b) { *b = scenario != 1; return scenario == 2 ? -8 : 0; }
Result srvIsServiceRegistered(bool *b, const char *name) {
    assert(!strcmp(name, "mvd:STD"));
    *b = scenario != 3; return scenario == 4 ? -8 : 0;
}
void *linearMemAlign(unsigned n, unsigned align) {
    ++allocCalls;
    if ((scenario == 5 && allocCalls == 1) || (scenario == 6 && allocCalls == 2))
        return nullptr;
    void *p = nullptr;
    assert(posix_memalign(&p, align, n) == 0);
    ++allocated; return p;
}
void linearFree(void *p) { assert(p); --allocated; free(p); }
Result GSPGPU_FlushDataCache(void *, unsigned) {
    ++flushCalls;
    return (scenario == 7 && flushCalls == 1) ||
           (scenario == 8 && flushCalls == 2) ? -8 : 0;
}
Result GSPGPU_InvalidateDataCache(void *, unsigned) { return scenario == 11 ? -8 : 0; }
Result mvdstdInit(int mode, int input, int output, unsigned size, void *) {
    assert(mode == MVDMODE_COLORFORMATCONV && input == MVD_INPUT_YUYV422);
    assert(output == MVD_OUTPUT_BGR565 && size == 0);
    ++initCalls; return scenario == 9 ? -8 : 0;
}
void mvdstdExit() { ++exitCalls; }
void mvdstdGenerateDefaultConfig(MVDSTD_Config *c, unsigned w, unsigned h,
        unsigned ow, unsigned oh, u32 *in, u32 *out, u32 *other) {
    assert(w == ow && h == oh && other == nullptr);
    c->out = (u16*)out; c->in = (u8*)in; c->pixels = w*h;
}
Result mvdstdConvertImage(MVDSTD_Config *c) {
    if (scenario == 10) return -8;
    for (unsigned i = 0; i < c->pixels; i++) {
        assert(c->in[i*2+1] == 128);
        assert(c->in[i*2] == (i < c->pixels/2 ? 0 : 255));
        if (scenario == 12) continue;
        unsigned dst = scenario == 15 ? c->pixels-1-i : i;
        c->out[dst] = c->in[i*2] == 0 ? 0 : 0xffff;
        if (scenario == 13) c->out[dst] = 0xffff;
    }
    if (scenario == 14) c->out[1] = 0x1234;
    return MVD_STATUS_OK;
}
__PRODUCTION_FUNCTION__
int main() {
    for (scenario = 0; scenario <= 15; scenario++) {
        allocated = allocCalls = initCalls = exitCalls = flushCalls = 0;
        u32 pixel = 0x12345678;
        bool ok = prepareMaterialTexel(pixel);
        assert(ok == (scenario == 0 || scenario == 15));
        assert(pixel == (ok ? 0xffffffff : 0));
        assert(allocated == 0);
        assert(exitCalls == (initCalls && scenario != 9 ? 1 : 0));
        if (scenario >= 1 && scenario <= 4) assert(allocCalls == 0);
    }
}
'''
        device = (ROOT / 'common/librw/src/3ds/3dsdevice.cpp').read_text()
        start = device.index('static bool\nprepareMaterialTexel')
        end = device.index('\nbool\ninitialiseMaterialState', start)
        source = source.replace('__PRODUCTION_FUNCTION__', device[start:end])
        with tempfile.TemporaryDirectory(prefix='regta-mvd-test-') as directory:
            temp = Path(directory)
            (temp / '3ds.h').write_text('// mocked by the test translation unit\n')
            (temp / 'test.cpp').write_text(source)
            subprocess.run(['c++', '-std=c++11', '-fsanitize=address,undefined',
                            '-I', str(temp), '-I', str(ROOT), str(temp / 'test.cpp'),
                            '-o', str(temp / 'test')], check=True)
            subprocess.run([str(temp / 'test')], check=True)

    def test_shared_renderer_dependency(self):
        device = (ROOT / 'common/librw/src/3ds/3dsdevice.cpp').read_text()
        self.assertIn('((u32*)whitetex.data)[i] = texel;', device)
        self.assertIn('threadCreate(prepareMaterialState', device)
        self.assertIn('threadJoin(worker, 1500ULL * 1000 * 1000)', device)
        self.assertIn('threadDetach(worker)', device)
        opening = device.split('openC3D(EngineOpenParams *openparams)')[1].split('closeC3D')[0]
        self.assertNotIn('prepareMaterialTexel', opening)
        self.assertNotIn('REGTA: MVD renderer unavailable', device)
        self.assertIn('bool initialiseMaterialState(void);',
                      (ROOT / 'common/librw/src/3ds/rw3ds.h').read_text())
        self.assertFalse((ROOT / 'common/3ds/HardwareRequirement.h').exists())
        self.assertFalse((ROOT / 'common/librw/src/3ds/mvd_material.h').exists())
        self.assertFalse((ROOT / 'common/librw/src/3ds/unsupported_device.h').exists())
        for game in ('III', 'miami', 'stories'):
            self.assertEqual((ROOT / game / 'vendor/librw').resolve(), ROOT / 'common/librw')
            source = (ROOT / game / 'src/core/Game.cpp').read_text()
            style = 'FONT_BANK' if game == 'III' else 'FONT_STANDARD'
            call = 'Initialise3DSRenderState();'
            self.assertEqual(source.count(call), 1)
            self.assertIn('rw::c3d::initialiseMaterialState()', source)
            self.assertIn('while(aptMainLoop())', source)
            self.assertIn('hidKeysDown() & KEY_B', source)
            self.assertIn('AsciiToUnicode("Device not supported", message)', source)
            self.assertIn('CFont::SetFontStyle(FONT_LOCALE(' + style + '))', source)
            self.assertNotIn('consoleInit', source)
            self.assertNotIn('EMULATOR_SKIP_STARTUP_MOVIES', source)
            self.assertLess(source.index('CFont::Initialise();'), source.index(call))
            self.assertLess(source.index('"Find big buildings"'), source.index(call))
            self.assertLess(source.index(call), source.index('CTheScripts::StartTestScript();'))
            if game == 'III':
                self.assertLess(source.index('CStreaming::Init();'), source.index(call))
            else:
                self.assertLess(source.index('CFileLoader::LoadLevel(datFile);'), source.index(call))
                loader = (ROOT / game / 'src/core/FileLoader.cpp').read_text()
                self.assertIn('CStreaming::Init();', loader)
        for game in ('miami', 'stories'):
            movie = (ROOT / game / 'src/skel/3ds/3dsmovie.cpp').read_text()
            self.assertLess(movie.index('srvIsServiceRegistered(&hasMvd'), movie.index('mvdstdInit('))


if __name__ == '__main__':
    unittest.main()
