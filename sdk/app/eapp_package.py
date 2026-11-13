#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Adapted from RepriseOS game-sdk/game_package.py and doom/artwork.py by lgreitis.
"""Pack an eApp image into the package folder Apple's host loads.

    eapp-pack --kind game --id hello --name Hello --image build/hello.eapp \\
        --icon icon.png --out build [FILE ...]

writes build/hello/ with Manifest.plist, hello.eapp, Hello.raw.lcd5 (the 55 x 55
RGB565 icon from the PNG) and every FILE, which the app reads by its name. The
folder goes to /Revola/Games/hello/ (--kind game) or /Revola/Apps/hello/
(--kind app). --zip also writes build/hello.zip with the folder under that root.

The module is importable: validate_image, manifest, artwork and write_entry
serve an app with a packaging script of its own.
"""

import argparse
import plistlib
import shutil
import struct
import sys
import zipfile
import zlib
from pathlib import Path

LOAD_ADDRESS = 0x18000000
MAX_IMAGE_BYTES = 0x100000
HEADER = (0x70706165, 0x10001000, 5, 0, 0)
ROOTS = {"game": "Revola/Games", "app": "Revola/Apps"}
ICON_SIDE = 55
ICON_STRIDE = ICON_SIDE * 2 + 2
VERSION = "0.2.0"


def validate_image(image):
    """Raise ValueError unless `image` is an eApp image the host accepts."""
    if not 0x28 <= len(image) <= MAX_IMAGE_BYTES:
        raise ValueError("image exceeds the eApp bounds")
    magic, version, count, reserved, imports, *entries = struct.unpack_from(
        "<10I", image
    )
    if (magic, version, count, reserved, imports) != HEADER:
        raise ValueError("unexpected eapp header")
    for entry in entries:
        if entry % 4 or not LOAD_ADDRESS + 0x28 <= entry < LOAD_ADDRESS + len(image):
            raise ValueError("an entry lies outside the image")


def manifest(
    name, package_id, executable, artwork, contents, heap_kib, version=VERSION
):
    """Manifest.plist as XML: `contents` maps a path to its bytes."""
    return plistlib.dumps(
        {
            "Name": name,
            "BuildIdentifier": package_id,
            "GUID": package_id,
            "Version": version,
            "HeapSize": heap_kib,
            "UserDataPath": package_id,
            "Files": [
                {"Path": path, "Size": len(data), "DRM": False, "Verify": False}
                for path, data in contents.items()
            ],
            "Platforms": [
                {
                    "PlatformID": 3,
                    "PlatformVersion": 1,
                    "BuildID": 1,
                    "ExecutablePath": executable,
                    "LaunchingArtwork": artwork,
                }
            ],
        },
        fmt=plistlib.FMT_XML,
        sort_keys=False,
    )


def write_entry(bundle, name, data):
    """A reproducible ZIP entry."""
    entry = zipfile.ZipInfo(str(name), date_time=(2026, 1, 1, 0, 0, 0))
    entry.compress_type = zipfile.ZIP_DEFLATED
    entry.external_attr = 0o100644 << 16
    bundle.writestr(entry, data)


def _paeth(a, b, c):
    p = a + b - c
    return min((a, b, c), key=lambda value: abs(p - value))


def artwork(png):
    """A 55 x 55, 8-bit RGB or RGBA, non-interlaced PNG as the host's RGB565
    artwork: a 16-byte header and the rows, transparent pixels over black."""
    if png[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("the icon is not a PNG")
    offset, channels = 8, 0
    compressed = bytearray()
    while offset < len(png):
        length, kind = struct.unpack_from(">I4s", png, offset)
        data = png[offset + 8 : offset + 8 + length]
        if (
            zlib.crc32(kind + data)
            != struct.unpack_from(">I", png, offset + 8 + length)[0]
        ):
            raise ValueError("PNG checksum mismatch")
        offset += length + 12
        if kind == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = (
                struct.unpack(">IIBBBBB", data)
            )
            fields = (width, height, depth, compression, filtering, interlace)
            if fields != (ICON_SIDE, ICON_SIDE, 8, 0, 0, 0) or color not in (2, 6):
                raise ValueError(
                    "the icon must be a non-interlaced 55 x 55 8-bit RGB or RGBA PNG"
                )
            channels = 4 if color == 6 else 3
        elif kind == b"IDAT":
            compressed.extend(data)
        elif kind == b"IEND":
            break
    if not channels:
        raise ValueError("PNG header missing")
    pixels = zlib.decompress(compressed)
    stride = ICON_SIDE * channels
    if len(pixels) != ICON_SIDE * (stride + 1):
        raise ValueError("unexpected PNG pixel length")
    result = bytearray(
        struct.pack("<III4s", ICON_SIDE, ICON_SIDE, ICON_STRIDE, b"565L")
    )
    previous = bytearray(stride)
    for y in range(ICON_SIDE):
        start = y * (stride + 1)
        filter_type = pixels[start]
        if filter_type > 4:
            raise ValueError("unsupported PNG row filter")
        row = bytearray(pixels[start + 1 : start + 1 + stride])
        for i in range(stride):
            left = row[i - channels] if i >= channels else 0
            above = previous[i]
            corner = previous[i - channels] if i >= channels else 0
            predictors = (
                0,
                left,
                above,
                (left + above) // 2,
                _paeth(left, above, corner),
            )
            row[i] = (row[i] + predictors[filter_type]) & 255
        for x in range(ICON_SIDE):
            p = x * channels
            alpha = row[p + 3] if channels == 4 else 255
            r, g, b = ((value * alpha + 127) // 255 for value in row[p : p + 3])
            result.extend(
                struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
            )
        result.extend(b"\0\0")
        previous = row
    return bytes(result)


def package(kind, package_id, name, image, icon, heap_kib, files, version=VERSION):
    """The package's files by name, Manifest.plist included."""
    if kind not in ROOTS:
        raise ValueError("kind must be game or app")
    validate_image(image)
    executable = f"{package_id}.eapp"
    artwork_name = f"{name}.raw.lcd5"
    contents = {executable: image, artwork_name: artwork(icon)}
    for path in files:
        if path.name in contents or path.name == "Manifest.plist":
            raise ValueError(f"{path.name} is taken")
        contents[path.name] = path.read_bytes()
    plist = manifest(
        name, package_id, executable, artwork_name, contents, heap_kib, version
    )
    return {"Manifest.plist": plist, **contents}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--kind", choices=sorted(ROOTS), required=True)
    parser.add_argument("--id", required=True, help="package id, the folder name")
    parser.add_argument("--name", required=True, help="what Extras lists")
    parser.add_argument("--image", type=Path, required=True, help="the .eapp image")
    parser.add_argument("--icon", type=Path, required=True, help="55 x 55 PNG")
    parser.add_argument("--heap-kib", type=int, default=5120)
    parser.add_argument("--version", default=VERSION, help="the package's Version")
    parser.add_argument("--out", type=Path, default=Path("build"))
    parser.add_argument("--zip", action="store_true", help="also write <out>/<id>.zip")
    parser.add_argument("files", type=Path, nargs="*", help="data files the app reads")
    args = parser.parse_args()
    try:
        contents = package(
            args.kind,
            args.id,
            args.name,
            args.image.read_bytes(),
            args.icon.read_bytes(),
            args.heap_kib,
            args.files,
            args.version,
        )
    except (ValueError, OSError) as error:
        print(f"eapp-pack: {error}", file=sys.stderr)
        return 1
    folder = args.out / args.id
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    for name, data in contents.items():
        (folder / name).write_bytes(data)
    if args.zip:
        with zipfile.ZipFile(args.out / f"{args.id}.zip", "w") as bundle:
            for name, data in contents.items():
                write_entry(bundle, f"{ROOTS[args.kind]}/{args.id}/{name}", data)
    print(f"Packed {folder} for /{ROOTS[args.kind]}/{args.id}/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
