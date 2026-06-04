"""Stage 1 -- the cheap 'digest' of a source file.

The digest is everything we can know without invoking libclang: timestamps,
hashes, the compile flags, and the module name / imports (found by a masked
regex so comments can't create false matches). It powers the cheap rungs of the
dirtiness ladder (L0 mtime, L1 byte hash, L2 normalised-source hash) and the
module dependency graph.
"""

from __future__ import annotations

import hashlib
import re
from pathlib import Path

from . import cppstrip
from .comprehension import _mask

_MODULE_RE = re.compile(r"(?m)^[ \t]*export[ \t]+module[ \t]+([A-Za-z0-9_.:]+)")
_MODULE_IMPL_RE = re.compile(r"(?m)^[ \t]*module[ \t]+([A-Za-z0-9_.:]+)[ \t]*;")
_IMPORT_RE = re.compile(r"(?m)^[ \t]*(?:export[ \t]+)?import[ \t]+([A-Za-z0-9_.:]+)[ \t]*;")


def light_scan(raw: str) -> tuple[str | None, list[str]]:
    """Return (module-provided, [modules-imported]) via masked regex."""
    masked = _mask(raw)
    module = None
    m = _MODULE_RE.search(masked)
    if m:
        module = m.group(1)
    else:
        mi = _MODULE_IMPL_RE.search(masked)
        if mi:
            module = mi.group(1)
    imports: list[str] = []
    for im in _IMPORT_RE.finditer(masked):
        name = im.group(1)
        if name not in imports:
            imports.append(name)
    return module, imports


def make(path: Path, flags_str: str) -> dict:
    raw = path.read_text(encoding="utf-8", errors="replace")
    module, imports = light_scan(raw)
    return {
        "source": str(path).replace("\\", "/"),
        "mtime": path.stat().st_mtime,
        "sha256": hashlib.sha256(raw.encode("utf-8", "replace")).hexdigest(),
        "norm_hash": hashlib.sha256(
            cppstrip.normalize(raw).encode("utf-8", "replace")).hexdigest(),
        "flags": flags_str,
        "provides": module,
        "requires": imports,
    }
