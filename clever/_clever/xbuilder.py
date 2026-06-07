"""Transpiled build driver: modules -> .hpp/.cpp -> classic compile -> link -> run.

This is the payoff of the transpiler: there are NO C++ modules at compile time
at all. Every source is lowered to ordinary header/source text, every `.cpp` is
compiled independently with plain `-c` (no `-fmodule-output`, no `.pcm`, no
ordering or BMI-consistency constraints), then archived/linked exactly like the
module builder (we reuse its link recipes).

Layout under the clever dir:
  xpp/   transpiled .hpp/.cpp (and _impl/ for non-module sources)
  xobj/  object files
  xout/  Rev.lib / CAMDemo.exe + copied DLLs
"""

from __future__ import annotations

import subprocess
from pathlib import Path

import concurrent.futures
import os
import threading

from . import transpile as tx
from .builder import Builder


class XBuilder(Builder):
    def __init__(self, manifest, repo, store, digests, verbose=False, jobs=0):
        super().__init__(manifest, repo, store, set(), digests=digests, verbose=verbose)
        self.xpp = store.root / "xpp"    # transpiled .hpp/.cpp
        self.xobj = store.root / "obj"   # object files
        self.out = store.root / "out"    # Rev.lib / exe + DLLs (base link uses self.out)
        self.cpp_of: dict[str, Path] = {}
        self.jobs = jobs if jobs and jobs > 0 else (os.cpu_count() or 4)
        self._lock = threading.Lock()
        self._n = 0

    def _tick(self, total: int, msg: str) -> None:
        with self._lock:
            self._n += 1
            print(f"  [{self._n:>3}/{total}] {msg}")

    # objects live under xobj/<target>/<safe rel>.obj (safe = drive-less, no '..')
    def _obj(self, rel: str) -> Path:
        return self.xobj / self.target_of[rel]["name"] / (self._safe_key(rel) + ".obj")

    # C++ modules don't leak macros across `import`, but our header `#include`s
    # do. Suppress windows.h's `max`/`min` macros (they clobber std::max/min)
    # to restore the module-world behaviour. Applied to both the parse and the
    # classic compile.
    # NOMINMAX: drop windows.h's max/min macros (clobber std::max/min).
    # WIN32_LEAN_AND_MEAN: stop windows.h pulling winsock1, which redefines
    # sockaddr etc. and clashes with the winsock2 the code includes directly.
    _LEAK_GUARD = ["-DNOMINMAX", "-DWIN32_LEAN_AND_MEAN"]

    def _base_flags(self, t: dict) -> list[str]:
        return super()._base_flags(t) + self._LEAK_GUARD

    def _parse_args(self, t: dict) -> list[str]:
        fl = []
        for d in t.get("include_dirs", []):
            fl.append("-I" + str(self.repo / d))
        for d in t.get("defines", []):
            fl.append("-D" + d)
        cf = t.get("compile_flags", [])
        i = 0
        while i < len(cf):
            if cf[i] == "-isystem" and i + 1 < len(cf):
                fl += ["-isystem", cf[i + 1]]; i += 2; continue
            i += 1
        return fl + self._LEAK_GUARD + ["-x", "c++", "-std=" + self.std]

    # -- stage: transpile everything --------------------------------------

    def _run_pool(self, items, fn):
        """Run fn over items across self.jobs threads; return list of results."""
        if self.jobs == 1:
            return [fn(x) for x in items]
        with concurrent.futures.ThreadPoolExecutor(max_workers=self.jobs) as ex:
            return list(ex.map(fn, items))

    def transpile_all(self) -> None:
        digests = {rel: {"provides": self.provides.get(rel)} for rel in self.rels}
        mhpp = tx.build_module_hpp_map(digests, self.xpp)
        total = len(self.rels)
        self._n = 0
        extra = self.manifest.get("comprehend", {}).get("extra_args", [])
        print(f"[1/3] Transpiling {total} sources (libclang, -j{self.jobs})...")

        def work(rel):
            t = self.target_of[rel]
            if rel.endswith(".ixx"):
                _, cpp = tx.transpile(rel, self.repo, self.xpp, mhpp, self._parse_args(t) + extra)
            else:
                cpp = tx.transpile_impl(rel, self.repo, self.xpp, mhpp)
            self._tick(total, f"XPP {rel}")
            return rel, cpp

        for rel, cpp in self._run_pool(self.rels, work):
            if cpp:
                self.cpp_of[rel] = cpp
        print(f"      transpiled into {self.xpp}")

    # -- stage: classic compile -------------------------------------------

    def _compile_classic(self, rel: str, total: int = 0) -> bool:
        t = self.target_of[rel]
        cpp = self.cpp_of.get(rel)
        if cpp is None:
            return True
        obj = self._obj(rel)
        obj.parent.mkdir(parents=True, exist_ok=True)
        cmd = [self.cxx, *self._base_flags(t), "-x", "c++", "-c", str(cpp), "-o", str(obj)]
        self._tick(total, f"CXX {t['name']}/{Path(rel).name}")
        if self.verbose:
            print("       " + " ".join(cmd))
        p = subprocess.run(cmd, capture_output=True, text=True, cwd=str(self.repo))
        if p.returncode != 0:
            with self._lock:
                print(f"FAILED: {rel}\n{p.stdout}\n{p.stderr}")
            return False
        if p.stderr.strip():
            with self._lock:
                print(p.stderr)
        with self._lock:
            self.compiled.add(rel)
        return True

    # -- orchestration -----------------------------------------------------

    def build(self) -> bool:
        self.transpile_all()
        total = len(self.rels)
        self._n = 0
        print(f"[2/3] Compiling {total} transpiled units (classic, no modules, -j{self.jobs})...")
        results = self._run_pool(self.rels, lambda r: self._compile_classic(r, total))
        if not all(results):
            return False
        # Link: static libs first, then executables (reusing base link recipes).
        print("[3/3] Linking...")
        for t in sorted(self.manifest["targets"], key=lambda x: 0 if x["kind"] == "static" else 1):
            ok = self._link_static(t) if t["kind"] == "static" else self._link_exe(t)
            if not ok:
                return False
            self.__dict__.setdefault("_relinked", set()).add(t["name"])
        return True
