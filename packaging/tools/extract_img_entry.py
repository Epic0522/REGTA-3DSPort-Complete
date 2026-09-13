#!/usr/bin/env python3
"""Extract named entries from a GTA III/VC IMG v1 archive.

The companion DIR contains fixed 32-byte records: sector offset, sector count,
and a 24-byte NUL-terminated filename. IMG sectors are always 2048 bytes.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


SECTOR_SIZE = 2048
ENTRY = struct.Struct("<II24s")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("directory", type=Path)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("names", nargs="+")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    image = args.image.resolve()
    directory = args.directory.resolve()
    output_dir = args.output_dir.resolve()
    wanted = {name.casefold(): name for name in args.names}

    raw_dir = directory.read_bytes()
    if len(raw_dir) % ENTRY.size:
        raise SystemExit(f"invalid DIR size: {len(raw_dir)}")

    records: dict[str, tuple[int, int, str]] = {}
    for offset in range(0, len(raw_dir), ENTRY.size):
        sector, count, raw_name = ENTRY.unpack_from(raw_dir, offset)
        name = raw_name.split(b"\0", 1)[0].decode("ascii")
        records[name.casefold()] = (sector, count, name)

    missing = sorted(set(wanted) - set(records))
    if missing:
        raise SystemExit("missing IMG entries: " + ", ".join(missing))

    image_size = image.stat().st_size
    output_dir.mkdir(parents=True, exist_ok=True)
    with image.open("rb") as stream:
        for key in wanted:
            sector, count, archive_name = records[key]
            byte_offset = sector * SECTOR_SIZE
            byte_count = count * SECTOR_SIZE
            if byte_offset + byte_count > image_size:
                raise SystemExit(f"out-of-bounds IMG entry: {archive_name}")
            stream.seek(byte_offset)
            payload = stream.read(byte_count)
            if len(payload) != byte_count:
                raise SystemExit(f"short read for IMG entry: {archive_name}")
            output = output_dir / wanted[key]
            output.write_bytes(payload)
            print(
                f"{archive_name}: sector={sector} count={count} "
                f"bytes={byte_count} -> {output}"
            )


if __name__ == "__main__":
    main()
