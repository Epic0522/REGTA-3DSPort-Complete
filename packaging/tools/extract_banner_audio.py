#!/usr/bin/env python3
"""Copy the encoded CWAV from an existing CBMD banner without re-encoding."""
import argparse
import struct
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("banner", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
data = args.banner.read_bytes()
if len(data) < 0x88 or data[:4] != b"CBMD":
    parser.error("Input is not a CBMD banner")
offset = struct.unpack_from("<I", data, 0x84)[0]
if offset < 0x88 or offset + 16 > len(data) or data[offset:offset + 4] != b"CWAV":
    parser.error("Banner does not contain a valid CWAV")
size = struct.unpack_from("<I", data, offset + 12)[0]
if size < 16 or offset + size > len(data):
    parser.error("Invalid CWAV size")
with args.output.open("xb") as output:
    output.write(data[offset:offset + size])
