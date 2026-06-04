#!/usr/bin/env python3
"""clever entry point.

Usage:
    python clever/clever.py build
    python clever/clever.py build CAMDemo -j8
    python clever/clever.py build --dry-run
    python clever/clever.py scan
    python clever/clever.py why Rev.OS.File
    python clever/clever.py clean

Run from the repository root.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from _clever.cli import main  # noqa: E402

if __name__ == "__main__":
    raise SystemExit(main())
