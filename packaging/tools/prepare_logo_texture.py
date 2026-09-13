#!/usr/bin/env python3
"""Remove transparent-pixel fringe colours from a banner logo PNG.

The visible RGBA pixels are preserved byte-for-byte.  RGB is propagated from
the nearest visible pixel into alpha-zero texels so bilinear GPU sampling does
not pull a white document background into the logo edge.
"""

from __future__ import annotations

import argparse
from collections import deque
from pathlib import Path

from PIL import Image


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("input_png", type=Path)
    parser.add_argument("output_png", type=Path)
    parser.add_argument(
        "--size",
        type=int,
        help="premultiplied-alpha Lanczos resize to SIZE x SIZE before edge bleed",
    )
    parser.add_argument(
        "--reinforce-lcs-g-edge",
        action="store_true",
        help="preserve the one-pixel white outside edge on the 128x128 LCS g",
    )
    return parser.parse_args()


def resize_premultiplied(source: Image.Image, size: int) -> Image.Image:
    if size <= 0:
        raise ValueError("--size must be positive")
    premultiplied = []
    for red, green, blue, alpha in source.getdata():
        premultiplied.append(
            (
                (red * alpha + 127) // 255,
                (green * alpha + 127) // 255,
                (blue * alpha + 127) // 255,
                alpha,
            )
        )
    image = Image.new("RGBA", source.size)
    image.putdata(premultiplied)
    image = image.resize((size, size), Image.Resampling.LANCZOS)

    straight = []
    for red, green, blue, alpha in image.getdata():
        if alpha == 0:
            straight.append((0, 0, 0, 0))
            continue
        straight.append(
            (
                min(255, (red * 255 + alpha // 2) // alpha),
                min(255, (green * 255 + alpha // 2) // alpha),
                min(255, (blue * 255 + alpha // 2) // alpha),
                alpha,
            )
        )
    output = Image.new("RGBA", image.size)
    output.putdata(straight)
    return output


def reinforce_lcs_g_edge(
    pixels: list[tuple[int, int, int, int]], width: int, height: int
) -> None:
    """Keep HOME's final bilinear reduction from swallowing the g's white rim."""
    if (width, height) != (128, 128):
        raise ValueError("LCS g-edge reinforcement requires a 128x128 logo")

    for y in range(14, 47):
        white_edge_x = next(
            (
                x
                for x in range(10, 23)
                if pixels[y * width + x][3] >= 128
                and min(pixels[y * width + x][:3]) >= 230
            ),
            None,
        )
        if white_edge_x is None:
            continue
        source_alpha = pixels[y * width + white_edge_x][3]
        outside = y * width + white_edge_x - 1
        outside_alpha = pixels[outside][3]
        pixels[outside] = (
            255,
            255,
            255,
            max(outside_alpha, round(source_alpha * 0.9)),
        )


def main() -> None:
    args = parse_args()
    source = Image.open(args.input_png).convert("RGBA")
    if args.size is not None and source.size != (args.size, args.size):
        source = resize_premultiplied(source, args.size)
    width, height = source.size
    pixels = list(source.getdata())
    if args.reinforce_lcs_g_edge:
        reinforce_lcs_g_edge(pixels, width, height)
    queue: deque[int] = deque()
    visited = bytearray(width * height)

    for index, pixel in enumerate(pixels):
        if pixel[3] != 0:
            visited[index] = 1
            queue.append(index)

    while queue:
        index = queue.popleft()
        x = index % width
        y = index // width
        for neighbour in (
            index - 1 if x else -1,
            index + 1 if x + 1 < width else -1,
            index - width if y else -1,
            index + width if y + 1 < height else -1,
        ):
            if neighbour < 0 or visited[neighbour]:
                continue
            visited[neighbour] = 1
            red, green, blue, _ = pixels[index]
            pixels[neighbour] = (red, green, blue, 0)
            queue.append(neighbour)

    output = Image.new("RGBA", source.size)
    output.putdata(pixels)
    args.output_png.parent.mkdir(parents=True, exist_ok=True)
    output.save(args.output_png, optimize=True)
    print(
        f"prepared {args.output_png.resolve()}: {width}x{height}, "
        "visible RGBA preserved, alpha-zero RGB edge-bled"
    )


if __name__ == "__main__":
    main()
