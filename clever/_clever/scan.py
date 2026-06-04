"""Module dependency scanning.

For every translation unit we need to know which named modules it *provides*
and which it *requires*. We get this from clang's own scanner
(`clang-scan-deps -format=p1689`), which understands `export module`,
`import`, partitions and `export import` correctly -- far more robust than a
regex.

Scanning is the second-most expensive thing after compiling, so results are
cached and only recomputed when a file's *normalised* contents change (a
comment-only edit will not re-trigger a scan).
"""

from __future__ import annotations

import json
import subprocess
from dataclasses import dataclass
from pathlib import Path

from .cppstrip import normalize


@dataclass
class ScanResult:
    source: Path
    provides: str | None          # module name this unit defines, if any
    requires: list[str]           # module names imported (direct)


def _norm_hash(source: Path) -> str:
    import hashlib

    try:
        text = source.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""
    return hashlib.sha256(normalize(text).encode("utf-8")).hexdigest()


def scan_one(compiler: str, unit_flags: list[str], source: Path) -> ScanResult:
    """Run clang-scan-deps on a single file and parse the p1689 output."""
    scan_deps = str(Path(compiler.replace("\\", "/")).with_name("clang-scan-deps.exe"))

    # clang does not recognise the .ixx extension on its own, so name the
    # language explicitly (clever strips CMake's -x from the harvested flags).
    lang = ["-x", "c++-module"] if source.suffix == ".ixx" else []
    cmd = [
        scan_deps,
        "-format=p1689",
        "--",
        compiler,
        *unit_flags,
        *lang,
        "-c",
        str(source),
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        # Surface the scanner error but don't crash the whole build planning;
        # treat as "no info" so the caller can decide.
        raise RuntimeError(
            f"clang-scan-deps failed for {source}:\n{proc.stderr.strip()}"
        )

    data = json.loads(proc.stdout)
    provides: str | None = None
    requires: list[str] = []
    for rule in data.get("rules", []):
        for p in rule.get("provides", []):
            provides = p.get("logical-name")
        for r in rule.get("requires", []):
            name = r.get("logical-name")
            if name:
                requires.append(name)
    return ScanResult(source=source, provides=provides, requires=requires)


class Scanner:
    """Caches scan results keyed by normalised-source hash + flags."""

    def __init__(self, compiler: str, cache: dict):
        self.compiler = compiler
        self.cache = cache  # persistent dict: {str(path): {...}}

    def scan(self, unit_flags: list[str], source: Path) -> ScanResult:
        key = str(source)
        nh = _norm_hash(source)
        flags_key = " ".join(unit_flags)
        cached = self.cache.get(key)
        if cached and cached.get("norm_hash") == nh and cached.get("flags") == flags_key:
            return ScanResult(
                source=source,
                provides=cached.get("provides"),
                requires=list(cached.get("requires", [])),
            )

        result = scan_one(self.compiler, unit_flags, source)
        self.cache[key] = {
            "norm_hash": nh,
            "flags": flags_key,
            "provides": result.provides,
            "requires": result.requires,
        }
        return result
