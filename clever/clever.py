#!/usr/bin/env python3
"""clever entry point.

Usage:
    python clever/clever.py init           # generate clever.json from build/
    python clever/clever.py check          # report what needs rebuilding
    python clever/clever.py check --json    # machine-readable
    python clever/clever.py show <file>     # dump one file's comprehension

Run from the repository root. `check` builds nothing; it only reports clever's
dirtiness analysis so its decisions can be validated by hand.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from _clever.cli import main  # noqa: E402

if __name__ == "__main__":
    raise SystemExit(main())
