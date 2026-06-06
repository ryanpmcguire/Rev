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

from . import transpile as tx
from .builder import Builder


class XBuilder(Builder):
    def __init__(self, manifest, repo, store, digests, verbose=False):
        super().__init__(manifest, repo, store, set(), digests=digests, verbose=verbose)
        self.xpp = store.root / "xpp"
        self.xobj = store.root / "xobj"
        self.out = store.root / "xout"   # base link methods use self.out
        self.cpp_of: dict[str, Path] = {}

    # objects live under xobj/<target>/<rel>.obj
    def _obj(self, rel: str) -> Path:
        return self.xobj / self.target_of[rel]["name"] / (rel + ".obj")

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
        return fl + ["-x", "c++", "-std=" + self.std]

    # -- stage: transpile everything --------------------------------------

    def transpile_all(self) -> None:
        digests = {rel: {"provides": self.provides.get(rel)} for rel in self.rels}
        mhpp = tx.build_module_hpp_map(digests, self.xpp)
        n_mod = n_impl = 0
        for rel in self.rels:
            t = self.target_of[rel]
            if rel.endswith(".ixx"):
                pa = self._parse_args(t) + self.manifest.get("comprehend", {}).get("extra_args", [])
                hpp, cpp = tx.transpile(rel, self.repo, self.xpp, mhpp, pa)
                if cpp:
                    self.cpp_of[rel] = cpp
                    n_mod += 1
            else:
                self.cpp_of[rel] = tx.transpile_impl(rel, self.repo, self.xpp, mhpp)
                n_impl += 1
        print(f"Transpiled {n_mod} modules + {n_impl} impl sources into {self.xpp}")

    # -- stage: classic compile -------------------------------------------

    def _compile_classic(self, rel: str) -> bool:
        t = self.target_of[rel]
        cpp = self.cpp_of.get(rel)
        if cpp is None:
            return True
        obj = self._obj(rel)
        obj.parent.mkdir(parents=True, exist_ok=True)
        cmd = [self.cxx, *self._base_flags(t), "-x", "c++", "-c", str(cpp), "-o", str(obj)]
        print(f"  CXX  {t['name']}/{Path(rel).name}")
        if self.verbose:
            print("       " + " ".join(cmd))
        p = subprocess.run(cmd, capture_output=True, text=True, cwd=str(self.repo))
        if p.returncode != 0:
            print(f"FAILED: {rel}\n{p.stdout}\n{p.stderr}")
            return False
        if p.stderr.strip():
            print(p.stderr)
        self.compiled.add(rel)
        return True

    # -- orchestration -----------------------------------------------------

    def build(self) -> bool:
        self.transpile_all()
        print(f"Compiling {len(self.rels)} transpiled translation units (classic, no modules)...")
        for rel in self.rels:
            if not self._compile_classic(rel):
                return False
        # Link: static libs first, then executables (reusing base link recipes).
        for t in sorted(self.manifest["targets"], key=lambda x: 0 if x["kind"] == "static" else 1):
            ok = self._link_static(t) if t["kind"] == "static" else self._link_exe(t)
            if not ok:
                return False
            self.__dict__.setdefault("_relinked", set()).add(t["name"])
        return True
