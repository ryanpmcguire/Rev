"""Tiny JSON-backed persistent cache.

Holds everything clever needs to make incremental decisions across runs:

  scan   -- per-file clang-scan-deps results (keyed by normalised hash)
  files  -- per-file build state: normalised source hash, flags hash, and the
            BMI hashes of every module it depended on at its last successful
            compile (this is what lets us detect "a dependency's *interface*
            changed since I was built")
  bmi    -- last known content hash of each module's produced .pcm
"""

from __future__ import annotations

import json
from pathlib import Path


class Cache:
    def __init__(self, path: Path):
        self.path = path
        self.data: dict = {"scan": {}, "files": {}, "bmi": {}}
        if path.exists():
            try:
                self.data = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError):
                pass
        self.data.setdefault("scan", {})
        self.data.setdefault("files", {})
        self.data.setdefault("bmi", {})

    @property
    def scan(self) -> dict:
        return self.data["scan"]

    @property
    def files(self) -> dict:
        return self.data["files"]

    @property
    def bmi(self) -> dict:
        return self.data["bmi"]

    def save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.path.with_suffix(self.path.suffix + ".tmp")
        tmp.write_text(json.dumps(self.data, indent=1), encoding="utf-8")
        tmp.replace(self.path)
