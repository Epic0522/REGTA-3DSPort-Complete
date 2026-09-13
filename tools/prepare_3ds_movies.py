#!/usr/bin/env python3
"""Convert re3/reVC Logo.mpg and GTAtitles.mpg to the 3DS startup format."""

import argparse
import pathlib
import struct
import subprocess
import tempfile


MOVIES = ("Logo", "GTAtitles")
FPS = 24


def split_annex_b(data: bytes):
    starts = []
    i = 0
    while i + 3 < len(data):
        if data[i : i + 4] == b"\0\0\0\1":
            starts.append((i, 4))
            i += 4
        elif data[i : i + 3] == b"\0\0\1":
            starts.append((i, 3))
            i += 3
        else:
            i += 1
    for index, (start, prefix_len) in enumerate(starts):
        end = starts[index + 1][0] if index + 1 < len(starts) else len(data)
        payload = data[start + prefix_len : end]
        if payload:
            yield b"\0\0\1" + payload


def convert_movie(ffmpeg: str, source: pathlib.Path, output: pathlib.Path, name: str):
    input_file = source / f"{name}.mpg"
    if not input_file.is_file():
        raise FileNotFoundError(input_file)

    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="re3ctr-movie-") as tmp:
        raw_h264 = pathlib.Path(tmp) / f"{name}.h264"
        subprocess.run(
            [
                ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", str(input_file),
                "-an", "-vf",
                f"fps={FPS},scale=320:240:flags=lanczos,transpose=clock",
                "-c:v", "libx264", "-profile:v", "baseline", "-level", "3.0",
                "-pix_fmt", "yuv420p", "-preset", "slow", "-b:v", "600k",
                "-maxrate", "750k", "-bufsize", "1500k", "-g", "48", "-bf", "0",
                "-f", "h264", str(raw_h264),
            ],
            check=True,
        )
        subprocess.run(
            [
                ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", str(input_file),
                "-vn", "-ac", "1", "-ar", "32000", "-c:a", "pcm_s16le",
                "-f", "s16le", str(output / f"{name}.pcm"),
            ],
            check=True,
        )

        packets = list(split_annex_b(raw_h264.read_bytes()))
        if not packets:
            raise RuntimeError(f"No H.264 NAL units found in {raw_h264}")
        with (output / f"{name}.3mv").open("wb") as dst:
            dst.write(struct.pack("<8sHHII", b"R3MVD01", 240, 320, FPS, 1))
            for packet in packets:
                dst.write(struct.pack("<I", len(packet)))
                dst.write(packet)
            dst.write(struct.pack("<I", 0))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    args = parser.parse_args()
    for movie in MOVIES:
        convert_movie(args.ffmpeg, args.source, args.output, movie)


if __name__ == "__main__":
    main()
