#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the core recipe, the Apple companion and the Rockbox bootloader."""

import argparse
import json
import os
import shutil
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path

import setup
from recipes.companion import build_recipe as build_companion_recipe
from recipes.osos import build_recipe
from toolchain import run
from version import identity

import firmware

ROOT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class BuildConfig:
    work: Path
    prefix: str
    jobs: int
    assembler: str


def compiler_prefix(value, version):
    compiler = shutil.which(value + "gcc")
    if not compiler:
        raise ValueError(
            "ARM GCC missing; supply --cross-prefix /path/to/arm-elf-eabi-"
        )
    prefix = str(Path(compiler).absolute())[:-3]
    if run([prefix + "gcc", "-dumpfullversion"], ROOT).strip() != version:
        raise ValueError("This target requires ARM GCC " + version)
    return prefix


def assemble(config, files, out, inputs, nor=None):
    """Apply a recipe to local Apple inputs. TODO: replace the RepriseOS CLI with revola's."""
    config.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="assemble-", dir=config.work) as temporary:
        image = Path(temporary) / out.name
        command = [
            config.assembler,
            "bundle",
            "apply-recipe",
            "--recipe",
            files["recipe"],
            "--data",
            files["data"],
            "--inputs",
            str(inputs.resolve()),
            "--out",
            str(image),
        ]
        if nor is not None:
            command += ["--nor", str(nor.resolve())]
        subprocess.run(command, check=True)
        image.replace(out)


def build_osos(config, out, inputs, recipe_only=False, selftest=False):
    print("Building core recipe...", flush=True)
    fingerprint = firmware.load()["inputs"]["osos.bin"]
    image = None
    if not recipe_only:
        image = (inputs / "osos.bin").read_bytes()
        firmware.verify(image, fingerprint, "osos.bin")
    recipe = build_recipe(
        ROOT / "core",
        firmware.DIRECTORY,
        config.work / "core",
        fingerprint,
        config.prefix,
        config.jobs,
        image,
        ["-DCORE_SELFTEST"] if selftest else [],
    )
    files = recipe.save(out, "osos")
    if not recipe_only:
        assemble(config, files, out / "osos-cfw.bin", inputs)


def build_apple(config, out, inputs, recipe_only=False, nor=None):
    print("Building companion recipe...", flush=True)
    recipe = build_companion_recipe(
        ROOT / "boot/companion",
        config.work / "companion",
        firmware.load()["inputs"],
        config.prefix,
        config.jobs,
    )
    files = recipe.save(out, "companion")
    if not recipe_only:
        assemble(config, files, out / "cfw-loader.bin", inputs, nor=nor)


def build_rockbox(config, out, rockbox):
    target, prefix, jobs = firmware.load(), config.prefix, config.jobs
    print("Building Rockbox bootloader...", flush=True)
    directory = config.work / "rockbox"
    directory.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["PATH"] = str(Path(prefix).parent) + os.pathsep + env.get("PATH", "")
    env["VERSION"] = target["rockbox_version"]
    makefile = directory / "Makefile"
    configuration = {
        "source": str(rockbox.resolve()),
        "prefix": prefix,
        "version": target["rockbox_version"],
    }
    stamp = directory / "configuration.json"
    if not stamp.is_file() or json.loads(stamp.read_text()) != configuration:
        shutil.rmtree(directory)
        directory.mkdir()
    if not makefile.exists():
        (directory / "configure.log").write_text(
            run(
                [rockbox / "tools/configure", "--target=ipod6g", "--type=b"],
                directory,
                env,
            )
        )
        text = makefile.read_text()
        line = next(
            line
            for line in text.splitlines()
            if line.startswith("export EXTRA_DEFINES=")
        )
        makefile.write_text(text.replace(line, line + " -DIPOD_CFW_FILE_BOOT", 1))
    stamp.write_text(json.dumps(configuration, sort_keys=True) + "\n")
    (directory / "build.log").write_text(run(["make", f"-j{jobs}"], directory, env))
    shutil.copyfile(
        directory / "bootloader-ipod6g.ipod", out / "bootloader-ipod6g.ipod"
    )

    packager = config.work / "packager"
    packager.mkdir(parents=True, exist_ok=True)
    sources = rockbox / "utils/mks5lboot"
    command = [
        "cc",
        "-O2",
        "-Wall",
        "-Wextra",
        '-DVERSION="' + target["rockbox_version"] + '"',
        sources / "dualboot.c",
        sources / "mkdfu.c",
        sources / "ipoddfu.c",
        sources / "main.c",
        "-o",
        "mks5lboot",
    ]
    if os.uname().sysname == "Darwin":
        command += ["-framework", "IOKit", "-framework", "CoreFoundation"]
    else:
        command += [
            "-DUSE_LIBUSBAPI",
            *run(["pkg-config", "--cflags", "--libs", "libusb-1.0"], packager).split(),
        ]
    (packager / "build.log").write_text(run(command, packager))
    (packager / "package.log").write_text(
        run(
            [
                packager / "mks5lboot",
                "--mkdfu-inst",
                out / "bootloader-ipod6g.ipod",
                out / "install-rockbox-cfw.dfu",
            ],
            out,
        )
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", help="Require a clean release tag at HEAD")
    parser.add_argument("--inputs", type=Path, default=ROOT / "inputs/firmware-2.0.5")
    parser.add_argument("--out", type=Path, default=ROOT / "build")
    parser.add_argument(
        "--nor", type=Path, help="Device NOR backup for companion personalization"
    )
    parser.add_argument(
        "target",
        nargs="?",
        choices=["all", "osos", "osos-recipe", "loader", "loader-recipe", "bootloader"],
        default="all",
    )
    parser.add_argument(
        "--cross-prefix", default=os.environ.get("CROSS_COMPILE", "arm-elf-eabi-")
    )
    parser.add_argument("--jobs", type=int, choices=range(1, 9), default=8)
    parser.add_argument(
        "--assembler", default="reprise", help="recipe assembler CLI on PATH"
    )
    parser.add_argument(
        "--selftest",
        action="store_true",
        help="compile the dispatcher self-test into the core (emulator runs only)",
    )
    args = parser.parse_args()
    try:
        target = firmware.load()
    except (OSError, ValueError) as error:
        parser.error(str(error))
    if args.target in ("all", "loader") and args.nor is None:
        parser.error("--nor is required to personalize the companion")
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    try:
        prefix = compiler_prefix(args.cross_prefix, target["toolchain"]["gcc"])
    except ValueError as error:
        parser.error(str(error))
    config = BuildConfig(ROOT / ".build", prefix, args.jobs, args.assembler)
    if args.target in ("all", "osos", "osos-recipe"):
        identity(ROOT, args.tag)
        build_osos(
            config, out, args.inputs, args.target == "osos-recipe", args.selftest
        )
    if args.target in ("all", "loader", "loader-recipe"):
        build_apple(config, out, args.inputs, args.target == "loader-recipe", args.nor)
    if args.target in ("all", "bootloader"):
        dependencies = json.loads((ROOT / "boot/dependencies.lock").read_text())
        rockbox = setup.verify("rockbox", dependencies["rockbox"])
        build_rockbox(config, out, rockbox)
    print(f"Built {args.target}: {out}")


if __name__ == "__main__":
    main()
