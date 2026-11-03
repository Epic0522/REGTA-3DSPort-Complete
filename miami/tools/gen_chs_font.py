#!/usr/bin/env python3
"""Bake a 20x20-cell Chinese glyph atlas for the 3DS Vice City port.

Reads a GXT (UTF-16LE, GTA text table) and a TrueType/TTC font, renders every
non-ASCII codepoint that appears in the text at 20x20 pixels, packs them into a
1024x1024 8-bit alpha atlas and writes a codepoint->slot lookup table.

Outputs (into miami/models/ by default):
  chsfont.a8    1024*1024 bytes, one alpha byte per pixel
  chsfont.map   u32 magic 'CHSM', u32 count, count * (u32 codepoint, u16 slot)
"""
import argparse
import os
import struct
import sys

ATLAS = 1024
CELL = 20
COLS = ATLAS // CELL          # 51
ROWS = ATLAS // CELL          # 51
SLOTS = COLS * ROWS           # 2601

# Render size before downsampling. Ink of a 40px Noto Sans CJK glyph lies in
# y in [12,52) and x in [0,40), so that box is cropped before the 2x downscale.
RENDER = 40
CROP_Y = 12


def read_gxt_strings(path):
    """Return every string of a GTA GXT file (main table + mission tables)."""
    data = open(path, "rb").read()
    strings = []

    def u32(off):
        return struct.unpack_from("<I", data, off)[0]

    def read_key_dat(off):
        magic, size = struct.unpack_from("<4sI", data, off)
        assert magic == b"TKEY", magic
        keys = [u32(off + 8 + i * 12) for i in range(size // 12)]
        off += 8 + size
        magic, size = struct.unpack_from("<4sI", data, off)
        assert magic == b"TDAT", magic
        dat = data[off + 8:off + 8 + size]
        off += 8 + size
        for k in keys:
            s = dat[k:].decode("utf-16le", "ignore").split("\x00", 1)[0]
            strings.append(s)
        return off

    magic, size = struct.unpack_from("<4sI", data, 0)
    assert magic == b"TABL", magic
    off = 8 + size
    off = read_key_dat(off)
    while off < len(data):
        off += 8  # mission table name
        if data[off:off + 4] != b"TKEY":
            break
        off = read_key_dat(off)
    return strings


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("gxt")
    ap.add_argument("--font", default="/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc")
    ap.add_argument("--font-index", type=int, default=0)
    ap.add_argument("--outdir", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "models"))
    ap.add_argument("--preview", default=None)
    args = ap.parse_args()

    from PIL import Image, ImageDraw, ImageFont

    strings = read_gxt_strings(args.gxt)
    cps = sorted({ord(ch) for s in strings for ch in s if ord(ch) >= 0x20})
    nonascii = [c for c in cps if c >= 0x80]
    print(f"{len(strings)} strings, {len(cps)} codepoints, {len(nonascii)} non-ASCII")

    if len(nonascii) > SLOTS:
        sys.exit(f"error: {len(nonascii)} glyphs do not fit into {SLOTS} cells")

    font = ImageFont.truetype(args.font, RENDER, index=args.font_index)
    atlas = Image.new("L", (ATLAS, ATLAS), 0)
    entries = []
    for i, cp in enumerate(nonascii):
        im = Image.new("L", (RENDER + 16, RENDER + 16), 0)
        ImageDraw.Draw(im).text((0, 0), chr(cp), font=font, fill=255)
        glyph = im.crop((0, CROP_Y, RENDER, CROP_Y + RENDER))
        glyph = glyph.resize((CELL, CELL), Image.LANCZOS)
        col, row = i % COLS, i // COLS
        atlas.paste(glyph, (col * CELL, row * CELL))
        entries.append((cp, row * COLS + col))

    # atlas is 8-bit luminance; reuse its bytes as the alpha channel
    outdir = os.path.normpath(args.outdir)
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "chsfont.a8"), "wb") as f:
        f.write(atlas.tobytes())
    with open(os.path.join(outdir, "chsfont.map"), "wb") as f:
        f.write(b"CHSM")
        f.write(struct.pack("<I", len(entries)))
        for cp, slot in entries:
            f.write(struct.pack("<IH", cp, slot))
    print(f"wrote {outdir}/chsfont.a8 ({ATLAS*ATLAS} bytes), {outdir}/chsfont.map "
          f"({len(entries)} entries), used rows={(len(entries)+COLS-1)//COLS}")

    if args.preview:
        sheet = Image.new("L", (ATLAS, ATLAS), 0)
        sheet.paste(atlas)
        sheet.resize((512, 512), Image.LANCZOS).save(args.preview)
        print("preview", args.preview)


if __name__ == "__main__":
    main()
