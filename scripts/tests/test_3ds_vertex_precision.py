#!/usr/bin/env python3
"""Exercise the real camera-relative world-matrix translation against a
float24 rounding simulation.

Regression test for: PICA200 vertex-shader arithmetic and uniforms are
float24 (16-bit mantissa). Uploading an absolute LCS world-space position
(world spans ~+-2000 units) as u_world's translation quantizes to 15-31mm
per vertex; peds re-skin every frame, so the rounding drifts continuously
-> visible PS1-style jitter. The fix folds the camera position into
u_world's translation on the CPU (float32) and zeroes u_view's translation,
so the float24 uniform only ever holds a small camera-relative delta.

This test slices the actual translation-handling lines out of
setWorldMatrix() and beginUpdate() in 3dsdevice.cpp (not hand-retyped, so
the test fails if someone reverts the fix), compiles them into a tiny host
harness alongside a float24 rounding-simulation helper, and asserts the
quantization error is large for an absolute coordinate but small for a
camera-relative one.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
DEVICE_CPP = ROOT / 'common/librw/src/3ds/3dsdevice.cpp'


def function(source, signature):
    start = source.index(signature)
    # Column-zero closing brace; good enough for these simple functions.
    end = source.index('\n}', start) + 2
    return source[start:end]


def between(text, start_marker, end_marker):
    start = text.index(start_marker)
    end = text.index(end_marker, start) + len(end_marker)
    return text[start:end]


class VertexPrecision(unittest.TestCase):
    def test_world_translation_is_camera_relative_and_low_error(self):
        source = DEVICE_CPP.read_text()

        set_world_matrix = function(source, 'setWorldMatrix(Matrix *mat)')
        world_translation = between(
            set_world_matrix,
            'uniformObject.world.r[0].w = raw.pos.x - cameraPosition.x;',
            'uniformObject.world.r[2].w = raw.pos.z - cameraPosition.z;')

        begin_update = function(source, 'beginUpdate(Camera *cam)')
        camera_capture = between(
            begin_update,
            'cameraPosition = cam->getFrame()->getLTM()->pos;',
            'cameraPosition = cam->getFrame()->getLTM()->pos;')
        view_translation = between(
            begin_update,
            'view.r[0].w = 0.0f;',
            'view.r[3].w = 1.0f;')

        stub = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

struct V3d { float x, y, z; };
struct Vec4 { float x, y, z, w; };
struct Mtx { Vec4 r[4]; };
struct RawMatrix { V3d pos; };
struct LTM { V3d pos; };
struct Frame { LTM ltm; LTM *getLTM() { return &ltm; } };
struct Camera { Frame frame; Frame *getFrame() { return &frame; } };

static V3d cameraPosition;
static RawMatrix raw;
static struct { Mtx world; } uniformObject;

/* Round a float32 to PICA200's float24 format: 1 sign + 7 exponent + 16
 * mantissa bits. Round-to-nearest over the 7 dropped mantissa bits. */
static float
round_float24(float v)
{
	if(v == 0.0f) return 0.0f;
	uint32_t bits;
	memcpy(&bits, &v, sizeof(bits));
	uint32_t sign = bits & 0x80000000u;
	int32_t  exp  = (bits >> 23) & 0xFFu;
	uint32_t mant = bits & 0x7FFFFFu;
	uint32_t rounded = (mant + (1u << 6)) >> 7;
	if(rounded > 0xFFFFu){
		rounded = 0;
		exp += 1;
	}
	uint32_t newbits = sign | ((uint32_t)exp << 23) | (rounded << 7);
	float result;
	memcpy(&result, &newbits, sizeof(result));
	return result;
}

static void
simulateWorldTranslation(void)
{
'''
        stub += world_translation
        stub += r'''
}

static void
simulateBeginUpdateTranslation(Camera *cam, Mtx &view)
{
'''
        stub += camera_capture
        stub += '\n'
        stub += view_translation
        stub += r'''
}
'''

        main = r'''
int main() {
	/* Before the fix: an absolute LCS world-space coordinate ~2300 units
	 * from origin (same order of magnitude as the ~2000-unit world span
	 * called out in the fix's background), rounded straight to float24
	 * the way the old code uploaded raw.pos directly. Crafted to land
	 * exactly on a float24 rounding half-step so the error is
	 * deterministic. */
	float absolute_pos = 2330.171875f;
	float rounded_absolute = round_float24(absolute_pos);
	float error_before_mm = std::fabs(rounded_absolute - absolute_pos) * 1000.0f;
	assert(error_before_mm > 10.0f);

	/* After the fix: camera and vertex are both far from the origin, but
	 * close to each other. beginUpdate() captures the camera position and
	 * zeroes u_view's translation... */
	Camera cam;
	cam.frame.ltm.pos = V3d{1234.5f, 0.0f, 0.0f};
	Mtx view;
	simulateBeginUpdateTranslation(&cam, view);
	assert(cameraPosition.x == 1234.5f);
	assert(view.r[0].w == 0.0f);
	assert(view.r[1].w == 0.0f);
	assert(view.r[2].w == 0.0f);
	assert(view.r[3].w == 1.0f);

	/* ...and setWorldMatrix() folds the camera position into u_world's
	 * translation, so only the small camera-relative delta ever reaches
	 * the float24 uniform. */
	raw.pos = V3d{1250.5003662109375f, 0.0f, 0.0f};
	simulateWorldTranslation();
	float delta = uniformObject.world.r[0].w;
	float rounded_delta = round_float24(delta);
	float error_after_mm = std::fabs(rounded_delta - delta) * 1000.0f;
	assert(error_after_mm < 0.2f);

	printf("error_before_mm=%f error_after_mm=%f\n", error_before_mm, error_after_mm);
}
'''

        with tempfile.TemporaryDirectory(prefix='regta-vertex-test-') as tmp:
            path, binary = Path(tmp) / 'test.cpp', Path(tmp) / 'test'
            path.write_text(stub + main)
            subprocess.run(['c++', '-std=c++11', '-fsanitize=address,undefined',
                            str(path), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
