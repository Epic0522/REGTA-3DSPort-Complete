#!/usr/bin/env python3
"""Prepare the nine VC radio stations as LCS-style 24 kHz mono IMA ADPCM."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

STATIONS = ("WILD", "FLASH", "KCHAT", "FEVER", "VROCK", "VCPR",
            "ESPANT", "EMOTION", "WAVE")
ADF_XOR = bytes(value ^ 0x22 for value in range(256))


def convert(source, destination, overwrite=False):
    source, destination = Path(source).resolve(), Path(destination).resolve()
    if source == destination or source in destination.parents:
        raise ValueError("Output must be outside the source audio directory")
    if shutil.which("ffmpeg") is None:
        raise ValueError("ffmpeg is required")
    files = {p.name.casefold(): p for p in source.iterdir() if p.is_file()}
    inputs = []
    for station in STATIONS:
        original = next((files[(station + ext).casefold()]
                         for ext in (".adf", ".mp3", ".wav")
                         if (station + ext).casefold() in files), None)
        if original is None:
            raise ValueError("Missing station: " + station)
        output = destination / (station + ".WAV")
        if output.exists() and not overwrite:
            raise ValueError(f"Output already exists: {output}; use --overwrite")
        inputs.append((original, output))

    destination.mkdir(parents=True, exist_ok=True)
    for original, output in inputs:
        print(f"24 kHz mono IMA ADPCM: {original.name} -> {output.name}", flush=True)
        fd, temporary = tempfile.mkstemp(prefix=".vc-radio-", suffix=".wav",
                                         dir=destination)
        os.close(fd)
        try:
            adf = original.suffix.casefold() == ".adf"
            command = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y"]
            command += ["-f", "mp3", "-i", "pipe:0"] if adf else ["-i", str(original)]
            command += ["-vn", "-ar", "24000", "-ac", "1", "-c:a", "adpcm_ima_wav",
                        "-map_metadata", "-1", temporary]
            if adf:
                process = subprocess.Popen(command, stdin=subprocess.PIPE)
                try:
                    with original.open("rb") as stream:
                        while True:
                            block = stream.read(65536)
                            if not block:
                                break
                            process.stdin.write(block.translate(ADF_XOR))
                    process.stdin.close()
                    if process.wait() != 0:
                        raise RuntimeError("ffmpeg failed: " + original.name)
                finally:
                    if process.poll() is None:
                        process.terminate()
                        process.wait()
            else:
                subprocess.run(command, check=True)
            os.replace(temporary, output)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_audio", help="Original VC Audio directory (read-only)")
    parser.add_argument("destination_audio", help="Separate output Audio directory")
    parser.add_argument("--overwrite", action="store_true", help="Replace the nine output WAVs")
    args = parser.parse_args()
    try:
        convert(args.source_audio, args.destination_audio, args.overwrite)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"VC radio conversion failed: {error}\n")
