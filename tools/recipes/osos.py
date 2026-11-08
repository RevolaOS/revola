# SPDX-License-Identifier: GPL-3.0-only
"""Compile the core and resolve its install-time patches into an assembly recipe."""

import json
import struct
from dataclasses import dataclass

import declarations
from declarations import Copy, Kind
from symbols import require
from toolchain import compile_payload
from recipes.segments import Input, Recipe

BASE = 0x08000000
IMAGE_HEADER_SIZE = 0x800
IMAGE_SIZE_FIELDS = (0xC, 0x10, 0x14)


@dataclass(frozen=True)
class CoreLayout:
    start: int
    end: int
    limit: int
    load_offset: int

    @property
    def reserved_size(self):
        return self.limit - self.start


def arm_call(address, destination):
    delta = destination - address - 8
    if address % 4 or destination % 4 or not -(1 << 25) <= delta < (1 << 25):
        raise ValueError("ARM call out of range or unaligned")
    return 0xEB000000 | ((delta >> 2) & 0xFFFFFF)


def core_units(source):
    return [
        (path.name, [])
        for path in sorted(source.iterdir())
        if path.suffix in (".c", ".S") and not path.name.endswith(".lds.S")
    ]


def build_recipe(source, firmware, directory, fingerprint, prefix, jobs):
    """Compile core/ and resolve its install-time patches against the pinned OSOS."""
    code, symbols = compile_payload(
        directory,
        source,
        core_units(source),
        "core.lds.S",
        "core",
        prefix,
        jobs,
        includes=(firmware, source.parent),
        thumb_symbols=True,
        just_symbols=firmware / "osos-2.0.5.ld",
    )
    patches = declarations.collect(directory, "core", prefix)
    used = symbols["__core_end"] - symbols["__core_start"]
    capacity = symbols["__core_limit"] - symbols["__core_start"]
    print(f"Core: {used:,} of {capacity:,} reserved bytes", flush=True)
    return build(code, patches, symbols, fingerprint)


def core_layout(code, symbols, original_size):
    layout = CoreLayout(
        require(symbols, "__core_start"),
        require(symbols, "__core_end"),
        require(symbols, "__core_limit"),
        require(symbols, "__osos_load_offset"),
    )
    if require(symbols, "__core_file_offset") != original_size:
        raise ValueError("Core file offset does not match OSOS")
    if (
        not layout.start <= layout.end <= layout.limit
        or len(code) > layout.reserved_size
    ):
        raise ValueError("Core exceeds reserved memory")
    return layout


def resolve_copy(patch, symbols, layout):
    if (
        patch.offset < 0
        or patch.size <= 0
        or patch.offset + patch.size > patch.array_size
    ):
        raise ValueError(f"Copy exceeds array: {patch.symbol}")
    if patch.source < BASE:
        raise ValueError("Copy source outside native firmware")
    destination = require(symbols, patch.symbol) - layout.start + patch.offset
    source = Input("osos", patch.source - BASE + layout.load_offset, patch.size)
    return destination, source


def resolve_write(patch, symbols, layout):
    address = patch.address
    value = patch.value if patch.kind == Kind.WORD else require(symbols, patch.symbol)
    if address % 4:
        raise ValueError(f"Unaligned patch at {address:#x}")
    if patch.kind != Kind.WORD and (
        value % 4 or not layout.start <= value <= layout.limit
    ):
        raise ValueError(f"Patch symbol outside core or unaligned: {patch.symbol}")
    if patch.kind in (Kind.CALL, Kind.JUMP) and value >= layout.end:
        raise ValueError(f"Hook outside core: {patch.symbol}")
    if patch.kind == Kind.CALL:
        value = arm_call(address, value)
    after = struct.pack("<I", value)
    if patch.kind == Kind.JUMP:
        after = struct.pack("<II", 0xE51FF004, value)
    offset = address - BASE + layout.load_offset
    if offset < layout.load_offset or len(patch.expected) != len(after):
        raise ValueError(f"Invalid native patch at {address:#x}")
    return offset, after


def resolve_patches(recipe, patches, symbols, layout):
    writes, copies = [], []
    for patch in patches:
        if isinstance(patch, Copy):
            copies.append(resolve_copy(patch, symbols, layout))
        else:
            offset, after = resolve_write(patch, symbols, layout)
            recipe.expect("osos", offset, patch.expected)
            writes.append((offset, after))
    return writes, copies


def size_header_writes(recipe, original_size, output_size):
    before = struct.pack("<I", original_size - IMAGE_HEADER_SIZE)
    after = struct.pack("<I", output_size - IMAGE_HEADER_SIZE)
    writes = []
    for offset in IMAGE_SIZE_FIELDS:
        recipe.expect("osos", offset, before)
        writes.append((offset, after))
    return writes


def build(code, patches, symbols, fingerprint):
    original_size = fingerprint["bytes"]
    layout = core_layout(code, symbols, original_size)
    recipe = Recipe({"osos": fingerprint})
    writes, copies = resolve_patches(recipe, patches, symbols, layout)
    writes.extend(
        size_header_writes(recipe, original_size, original_size + layout.reserved_size)
    )
    recipe.overlay(Input("osos", 0, original_size), writes)
    recipe.overlay(code, copies)
    recipe.zero(layout.reserved_size - len(code))
    return recipe
