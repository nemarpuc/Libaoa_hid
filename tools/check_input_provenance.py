#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 libaoahid contributors
"""Fail when the documentation misspells the CMake option prefix."""

from __future__ import annotations

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()

    failures: list[str] = []
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
