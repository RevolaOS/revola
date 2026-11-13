#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Guard the SDK's API version: a change under sdk/ needs a version bump.

    python3 tools/sdklock.py check    # fails when sdk/ changed at the same version
    python3 tools/sdklock.py update   # records the current files and version

sdk/api.lock holds the API version and the sha256 of every file the tarball
ships from sdk/, by path relative to sdk/ (the examples are not part of the SDK).
"""

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "sdk"
LOCK = SDK / "api.lock"


SKIPPED = {"api.lock", "examples", "build", "__pycache__", ".DS_Store"}


def api_version(header=None):
    text = (header or SDK / "extension/rv_api.h").read_text()
    major = re.search(r"#define RV_API_MAJOR (\d+)", text).group(1)
    minor = re.search(r"#define RV_API_MINOR (\d+)", text).group(1)
    return f"{major}.{minor}"


def digests(sdk=None):
    sdk = sdk or SDK
    files = sorted(
        p
        for p in sdk.rglob("*")
        if p.is_file() and not SKIPPED & set(p.relative_to(sdk).parts)
    )
    return {
        p.relative_to(sdk).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in files
    }


def current(sdk=None):
    sdk = sdk or SDK
    return {"api": api_version(sdk / "extension/rv_api.h"), "sha256": digests(sdk)}


def check(sdk=None, lock=None):
    """Raise when sdk/ differs from the lock without a new API version."""
    sdk, lock = sdk or SDK, lock or LOCK
    recorded = json.loads(lock.read_text())
    now = current(sdk)
    if now["sha256"] != recorded["sha256"] and now["api"] == recorded["api"]:
        changed = sorted(
            name
            for name in set(now["sha256"]) | set(recorded["sha256"])
            if now["sha256"].get(name) != recorded["sha256"].get(name)
        )
        raise ValueError(
            f"sdk/ changed ({', '.join(changed)}) at API {now['api']}: bump "
            "RV_API_MINOR or RV_API_MAJOR in sdk/extension/rv_api.h, then run "
            "tools/sdklock.py update"
        )
    if now["sha256"] != recorded["sha256"]:
        raise ValueError("sdk/api.lock is stale: run tools/sdklock.py update")


def update(sdk=None, lock=None):
    (lock or LOCK).write_text(json.dumps(current(sdk), indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("action", choices=["check", "update"])
    args = parser.parse_args()
    if args.action == "update":
        update()
        return 0
    try:
        check()
    except ValueError as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
