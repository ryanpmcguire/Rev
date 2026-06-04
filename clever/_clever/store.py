"""The `.clever/` artifact store.

A directory tree mirroring the project's source layout, holding one JSON file
per (source, stage). Stages:

  digest  -- cheap identity + module graph inputs (digest.py)
  ast     -- libclang comprehension (comprehension.py)
  graph   -- resolved provider/consumer file locations

Keeping every intermediate on disk, mirrored to the source tree, makes the
pipeline inspectable step by step (e.g. `.clever/Rev/src/Core/Color.ixx.ast.json`).
"""

from __future__ import annotations

import json
from pathlib import Path


class Store:
    def __init__(self, root: Path):
        self.root = root

    def _path(self, rel: str, stage: str) -> Path:
        return self.root / (rel + f".{stage}.json")

    def read(self, rel: str, stage: str) -> dict | None:
        p = self._path(rel, stage)
        if not p.exists():
            return None
        try:
            return json.loads(p.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return None

    def write(self, rel: str, stage: str, data: dict) -> None:
        p = self._path(rel, stage)
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(json.dumps(data, indent=1), encoding="utf-8")

    def has_any(self, rels, stage: str) -> bool:
        return any(self._path(r, stage).exists() for r in rels)
