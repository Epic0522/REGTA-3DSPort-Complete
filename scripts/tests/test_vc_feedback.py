#!/usr/bin/env python3
"""Host regressions for VC phone input and LCS-style stream preparation."""

import hashlib
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("vc_radio", ROOT / "miami/tools/convert_vc_radio_3ds.py")
radio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(radio)


def cpp_run(directory, source, args=()):
    cpp = directory / "test.cpp"
    exe = directory / "test"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++11", "-Wno-multichar", "-fsanitize=address,undefined",
                    str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe), *map(str, args)], check=True)


class VCFeedback(unittest.TestCase):
    def test_phone_mapping(self):
        source = (ROOT / "miami/src/core/Pad.cpp").read_text()
        start = source.index("\tconst bool standardAimOnFoot =")
        end = source.index("\tPCTempJoyState.LeftShoulder2", start)
        # Compile the production mapping, not a separately reimplemented formula.
        mapping = source[start:end]
        code = r'''
#include <cassert>
enum { CONTROL_STANDARD, CONTROL_CLASSIC };
enum { KEY_L=1, KEY_X=2, KEY_A=4, KEY_B=8, KEY_Y=16,
       KEY_DDOWN=32, KEY_DLEFT=64, KEY_DRIGHT=128, KEY_DUP=256 };
struct { int m_ControlMethod; bool m_bMenuActive; } FrontEndMenuManager;
struct State { int Cross, Circle, Square, Triangle, DPadDown, DPadLeft,
               DPadRight, DPadUp, LeftShoulder1; } PCTempJoyState;
namespace CHud { bool m_b3DSPhoneAnswerPrompt; }
static bool g3DSPhoneLConsumed, inVehicle;
void *FindPlayerPed() { return &inVehicle; }
void *FindPlayerVehicle() { return inVehicle ? &inVehicle : nullptr; }
#define nil nullptr
void map(unsigned held) {
''' + mapping + r'''
}
int main() {
    FrontEndMenuManager.m_ControlMethod = CONTROL_STANDARD;
    map(KEY_L); assert(PCTempJoyState.Circle && !PCTempJoyState.LeftShoulder1);
    CHud::m_b3DSPhoneAnswerPrompt = true;
    map(KEY_L); assert(!PCTempJoyState.Circle && PCTempJoyState.LeftShoulder1);
    map(KEY_L | KEY_X); assert(PCTempJoyState.Circle && PCTempJoyState.LeftShoulder1);
    CHud::m_b3DSPhoneAnswerPrompt = false;
    map(KEY_L); assert(!PCTempJoyState.Circle); // no shot when the call ends
    map(0); map(KEY_L); assert(PCTempJoyState.Circle);
    FrontEndMenuManager.m_ControlMethod = CONTROL_CLASSIC;
    map(KEY_L); assert(!PCTempJoyState.Circle && PCTempJoyState.LeftShoulder1);
    FrontEndMenuManager.m_ControlMethod = CONTROL_STANDARD;
    inVehicle = true;
    map(KEY_L); assert(!PCTempJoyState.Circle && PCTempJoyState.LeftShoulder1);
}
'''
        with tempfile.TemporaryDirectory(prefix="vc-input-test-") as tmp:
            cpp_run(Path(tmp), code)

    def test_radio_conversion_and_runtime_decoder(self):
        with tempfile.TemporaryDirectory(prefix="vc-radio-test-") as tmp:
            tmp = Path(tmp)
            original, output = tmp / "original", tmp / "converted"
            original.mkdir()
            mp3 = subprocess.check_output([
                "ffmpeg", "-v", "error", "-f", "lavfi", "-i", "sine=frequency=440:duration=0.5",
                "-ar", "44100", "-ac", "2", "-f", "mp3", "pipe:1"])
            adf = mp3.translate(radio.ADF_XOR)
            for station in radio.STATIONS:
                (original / (station.lower() + ".adf")).write_bytes(adf)
            for track in radio.AMBIENCE:
                (original / (track.lower() + ".mp3")).write_bytes(mp3)
            before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in original.iterdir()}
            radio.convert(original, output)
            self.assertEqual(before, {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in original.iterdir()})
            self.assertEqual(len(list(output.glob("*.WAV"))), len(radio.TRACKS))
            with self.assertRaises(ValueError):
                radio.convert(original, output)  # no accidental overwrite
            with self.assertRaises(ValueError):
                radio.convert(original, original / "nested")

            stream = (ROOT / "miami/src/audio/oal/stream.cpp").read_text()
            self.assertIn('const char *streamPath = real ? real : wavPath;', stream)
            self.assertIn('!strcasecmp(m_aFilename + filenameLength - 4, ".mp3")', stream)
            self.assertIn('if (!f)\n\t\t\t\treturn;', stream)
            # Match LCS's low-latency station startup: do not synchronously
            # decode the entire OpenAL queue every time the player retunes.
            converted = stream[stream.index('// Prefer the same low-cost continuous-stream format used by LCS'):
                               stream.index('#endif', stream.index('// Prefer the same low-cost continuous-stream format used by LCS'))]
            self.assertNotIn('m_bFullInitialQueue = true', converted)
            header = (ROOT / "miami/src/audio/oal/stream.h").read_text()
            decoder = header[header.index("class IDecoder"):header.index("class CStream")]
            start = stream.index("class CImaADPCMDecoder")
            wav = stream[start:stream.index("#ifdef AUDIO_OAL_USE_SNDFILE", start)]
            preamble = r'''
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <vector>
#include <algorithm>
using uint8=uint8_t; using int8=int8_t; using uint16=uint16_t;
using int16=int16_t; using uint32=uint32_t; using int32=int32_t;
#define nil nullptr
#define debug(...) ((void)0)
template<class A,class B> auto Min(A a,B b)->decltype(a+b) {return a<b?a:b;}
template<class A,class B,class C> A clamp(A a,B b,C c){return a<b?b:(a>c?c:a);}
struct {void SortStereo(void*,size_t){assert(false);}} SortStereoBuffer;
'''
            test = r'''
int main(int argc,char **argv) {
    for(int i=1;i<argc;i++) {
        CWavFile file(argv[i]); assert(file.IsOpened());
        assert(file.GetChannels()==1 && file.GetSampleRate()==24000);
        std::vector<uint8> buffer(file.GetBufferSize());
        uint32 bytes=0, count;
        while((count=file.Decode(buffer.data()))) {assert(count<=buffer.size()); bytes+=count;}
        assert(bytes==file.GetSampleCount()*2 && bytes>0);
        assert(file.Decode(buffer.data())==0);
        file.Seek(0); assert(file.Decode(buffer.data())>0);
        file.Seek(file.GetLength()/2); assert(file.Decode(buffer.data())>0);
    }
}
'''
            files = sorted(output.glob("*.WAV"))
            if os.environ.get("VC_RADIO_TEST_DIR"):
                files += sorted(Path(os.environ["VC_RADIO_TEST_DIR"]).glob("*.WAV"))
            cpp_run(tmp, preamble + decoder + wav + test, files)

    def test_mp3_player_disabled_on_3ds(self):
        for game in ('III', 'miami', 'stories'):
            dma = (ROOT / game / 'src/audio/DMAudio.cpp').read_text()
            start = dma.index('cDMAudio::IsMP3RadioChannelAvailable(void)')
            body = dma[start:dma.index('\n}', start) + 2]
            self.assertIn('#ifdef _3DS\n\treturn false;', body)
            sample = (ROOT / game / 'src/audio/sampman_oal.cpp').read_text()
            self.assertIn('#ifndef _3DS\n\t\t_FindMP3s();\n#endif', sample)

    def test_vc_radio_stream_is_persistent_on_3ds(self):
        stream_h = (ROOT / 'miami/src/audio/oal/stream.h').read_text()
        stream_cpp = (ROOT / 'miami/src/audio/oal/stream.cpp').read_text()
        sample = (ROOT / 'miami/src/audio/sampman_oal.cpp').read_text()
        self.assertIn('CStream(ALuint *sources, ALuint (&buffers)[NUM_STREAMBUFFERS]);', stream_h)
        self.assertIn('bool   Open(const char *filename', stream_h)
        self.assertIn('void   Close();', stream_h)
        self.assertIn('CStream::Open(const char *filename', stream_cpp)
        self.assertIn('aStream[0] = new CStream(ALStreamSources[0], ALStreamBuffers[0]);', sample)
        start = sample[sample.index('cSampleManager::StartStreamedFile'):sample.index('cSampleManager::StopStreamedFile')]
        self.assertIn('if (nStream == 0 && nFile != STREAMED_SOUND_RADIO_MP3_PLAYER)', start)
        self.assertIn('stream->Close();', start)
        self.assertIn('stream->Open(filename', start)
        self.assertIn('stream->BeginRadioStart()', start)
        self.assertIn('RadioStartThreadMain', stream_cpp)
        self.assertIn('threadCreate(RadioStartThreadMain', stream_cpp)

    def test_3ds_effects_headroom_is_early_but_not_normalized(self):
        for game, limit in (('III', 'MAXCHANNELS'), ('miami', 'MAXCHANNELS'),
                            ('stories', 'NUM_CHANNELS_GENERIC')):
            sample = (ROOT / game / 'src/audio/sampman_oal.cpp').read_text()
            self.assertIn('static uint16 gEffectsMixHeadroom3DS = 256;', sample)
            self.assertIn('activeEffects > 6', sample)
            self.assertIn('Max(192, 256 - (int32)(activeEffects - 6) * 4)', sample)
            self.assertIn('for (int32 i = 0; i < ' + limit + '; ++i)', sample)
        # Per-source gain falls gently, while total mixed level still rises.
        totals = []
        for voices in range(1, 41):
            gain = 256 if voices <= 6 else max(192, 256 - (voices - 6) * 4)
            totals.append(voices * gain)
        self.assertTrue(all(b > a for a, b in zip(totals, totals[1:])))


if __name__ == "__main__":
    unittest.main()
