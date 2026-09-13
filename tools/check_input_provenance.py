#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 libaoahid contributors
"""Fail when a preserved user-supplied source document changes."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


EXPECTED = {
    "docs/inputs/AOA_HID_GUIDE_ORIGINAL.md": "53b684de965f49f2cf2536dc9d2e66346ba92df06bd8af42b91db77e19591a49",
    "docs/inputs/DESIGN_ORIGINAL.md": "89475b0b6c539febbc5e51de76f4baaccb7657a09a7a708c58b3836eb48b4c4d",
    "docs/IMPLEMENTATION_PROMPTS.md": "8300f59379a4ff13e3503634d0f32f8da87330746f98125754cf4aa7d693cf78",
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()

    failures: list[str] = []
    for relative, expected in EXPECTED.items():
        path = args.source_root / relative
        try:
            actual = hashlib.sha256(path.read_bytes()).hexdigest()
        except OSError as error:
            failures.append(f"{relative}: cannot read: {error}")
            continue
        if actual != expected:
            failures.append(f"{relative}: expected {expected}, got {actual}")

    # A missing `A` in `AOAHID` is accepted by CMake as an unused cache
    # variable and silently leaves the real option at its default.  Keep the
    # command examples mechanically tied to the actual public option prefix.
    misspelled_prefix = "A" + "OHID_"
    documentation = [args.source_root / "README.md"]
    documentation.extend(sorted((args.source_root / "docs").glob("*.md")))
    for path in documentation:
        try:
            text = path.read_text(encoding="utf-8")
        except OSError as error:
            failures.append(f"{path.relative_to(args.source_root)}: cannot read: {error}")
            continue
        if misspelled_prefix in text:
            failures.append(
                f"{path.relative_to(args.source_root)}: contains misspelled CMake option prefix "
                f"{misspelled_prefix!r}; expected 'AOAHID_'"
            )

    if failures:
        for failure in failures:
            print(failure)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
