#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Render the core's Settings pane icons: 78 x 121 gray coverage masks with the
art fitted into the art box of the stock Settings icons, lit from the top and
mirrored in a fading reflection below. Extensions and Optimize are Cupertino
Icons glyphs (CupertinoIcons.ttf from the Flutter `cupertino_icons` package,
MIT, see LICENSE.cupertino-icons; the font is not in the repository). Info is
the Revola mark: a wheel around a core with one segment set apart, drawn from
its vector paths. Drawing uses ImageMagick's `magick`.

    python3 core/assets/render.py --font CupertinoIcons.ttf
    python3 core/assets/render.py --font CupertinoIcons.ttf --check
"""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
WIDTH, HEIGHT = 78, 121
# The art box of the stock icons, and the light across it.
ART_TOP, ART_BOTTOM, ART_CENTRE = 12, 62, (39, 37)
LIGHT_TOP, LIGHT_BOTTOM = 228, 155
# The reflection mirrors the art about the line below the art box, as the
# stock icons do, and fades from this share of the art at the bottom of the
# box's reflection to nothing on the last row.
MIRROR = 77.5
REFLECTION = (0.32, 0.0)
GRAY8 = (
    "-colorspace",
    "gray",
    "-depth",
    "8",
    "-define",
    "png:color-type=0",
    "-define",
    "png:bit-depth=8",
    "-define",
    "png:exclude-chunks=date,time",
)
# Icon file stem to a glyph (code point in the font and its reach: the longer
# side of the box the glyph is fitted into, centred on the art box) or to the
# Revola mark, which is fitted to the same reach as the glyphs.
GLYPHS = {
    "extensions": (0xF61D, 54),  # cube_fill
    "optimize": (0xF7F5, 52),  # speedometer
}
MARK_REACH = 52
# The Revola mark on its 256 x 256 artboard: the wheel and its separate segment
# are sectors between radii 96 and 59, the core a disc of radius 24. The visible
# silhouette spans x 32 to 227 and y 29 to 224.
MARK_SECTORS = (
    ((219.771, 147.441), 1, (108.559, 36.229), (114.984, 72.667), (183.333, 141.016)),
    ((148.899, 29.229), 0, (226.771, 107.101), (190.333, 113.526), (142.474, 65.667)),
)
MARK_CORE = ((125.229, 130.771), 24)
MARK_RADII = (96, 59)
MARK_BOUNDS = ((32, 227), (29, 224))
# Vector art is drawn this many times larger and resized down for coverage.
SUPERSAMPLE = 8


def magick(*args):
    subprocess.run(["magick", *map(str, args)], check=True)


def glyph_art(work, font, code, reach):
    """The glyph fitted into the art box, white on a black canvas."""
    glyph = work / f"{code:x}.png"
    magick(
        "-background",
        "black",
        "-fill",
        "white",
        "-font",
        font,
        "-pointsize",
        "400",
        f"label:{chr(code)}",
        "-trim",
        "+repage",
        "-resize",
        f"{reach}x{reach}",
        glyph,
    )
    art = work / "art.png"
    magick(
        "-size",
        f"{WIDTH}x{HEIGHT}",
        "xc:black",
        "(",
        glyph,
        "-background",
        "black",
        "-gravity",
        "Center",
        "-extent",
        f"{reach}x{reach}",
        ")",
        "-gravity",
        "NorthWest",
        "-geometry",
        f"+{ART_CENTRE[0] - reach // 2}+{ART_CENTRE[1] - reach // 2}",
        "-compose",
        "Over",
        "-composite",
        art,
    )
    return art


def mark_art(work):
    """The Revola mark fitted into the art box, white on a black canvas."""
    (left, right), (top, bottom) = MARK_BOUNDS
    scale = MARK_REACH / max(right - left, bottom - top)
    dx = ART_CENTRE[0] - (left + right) / 2 * scale
    dy = ART_CENTRE[1] - (top + bottom) / 2 * scale

    def point(xy):
        return f"{dx + xy[0] * scale:.3f},{dy + xy[1] * scale:.3f}"

    outer, inner = (f"{r * scale:.3f}" for r in MARK_RADII)
    paths = [
        f"path 'M {point(start)} A {outer} {outer} 0 {large} 1 {point(rim)} "
        f"L {point(back)} A {inner} {inner} 0 {large} 0 {point(end)} Z'"
        for start, large, rim, back, end in MARK_SECTORS
    ]
    (cx, cy), radius = MARK_CORE
    centre = point((cx, cy))
    edge = point((cx + radius, cy))
    art = work / "art.png"
    magick(
        "-size",
        f"{WIDTH * SUPERSAMPLE}x{HEIGHT * SUPERSAMPLE}",
        "xc:black",
        "-fill",
        "white",
        "-draw",
        f"scale {SUPERSAMPLE},{SUPERSAMPLE} {' '.join(paths)} circle {centre} {edge}",
        "-resize",
        f"{100 / SUPERSAMPLE}%",
        art,
    )
    return art


def render(out, work, art):
    """Light the art down the art box and add its reflection."""
    slope = (LIGHT_TOP - LIGHT_BOTTOM) / (ART_BOTTOM - ART_TOP)
    top = round(LIGHT_TOP + slope * ART_TOP)
    bottom = round(top - slope * (HEIGHT - 1))
    lit = work / "lit.png"
    magick(
        art,
        "(",
        "-size",
        f"{WIDTH}x{HEIGHT}",
        f"gradient:gray({top})-gray({bottom})",
        ")",
        "-compose",
        "Multiply",
        "-composite",
        lit,
    )
    shift = round(2 * MIRROR - (HEIGHT - 1))
    start = round(2 * MIRROR - ART_BOTTOM)
    fade = work / "fade.png"
    magick(
        "-size",
        f"{WIDTH}x{HEIGHT}",
        "xc:black",
        "(",
        "-size",
        f"{WIDTH}x{HEIGHT - start}",
        f"gradient:gray({REFLECTION[0] * 100}%)-gray({REFLECTION[1] * 100}%)",
        ")",
        "-geometry",
        f"+0+{start}",
        "-composite",
        fade,
    )
    magick(
        lit,
        "(",
        lit,
        "-flip",
        "-roll",
        f"+0+{shift}",
        fade,
        "-compose",
        "Multiply",
        "-composite",
        ")",
        "-compose",
        "Lighten",
        "-composite",
        *GRAY8,
        out,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument(
        "--font", default=os.environ.get("CUPERTINO_ICONS"), help="CupertinoIcons.ttf"
    )
    parser.add_argument("--check", action="store_true", help="fail when a file differs")
    args = parser.parse_args()
    if not args.font:
        sys.exit("give the font with --font or CUPERTINO_ICONS")
    with tempfile.TemporaryDirectory() as name:
        work = Path(name)
        for stem in [*GLYPHS, "info"]:
            final = HERE / f"{stem}.png"
            out = work / final.name if args.check else final
            if stem in GLYPHS:
                art = glyph_art(work, args.font, *GLYPHS[stem])
            else:
                art = mark_art(work)
            render(out, work, art)
            if args.check and out.read_bytes() != final.read_bytes():
                sys.exit(f"{final.name} is out of date: run {Path(__file__).name}")


if __name__ == "__main__":
    main()
