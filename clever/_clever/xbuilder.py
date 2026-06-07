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


def _transpile_worker(task):
    """Top-level (picklable) worker for process-based transpile parallelism.
    Transpiling is GIL-bound (libclang AST walk + tokenize in Python), so
    threads don't help; separate processes do."""
    from pathlib import Path
    from . import transpile as _tx
    rel, repo, xpp, mhpp, parse_args, project_root = task
    repo = Path(repo); xpp = Path(xpp)
    mhpp = {k: Path(v) for k, v in mhpp.items()}
    pr = Path(project_root) if project_root else None
    if rel.endswith(".ixx"):
        _, cpp, res = _tx.transpile(rel, repo, xpp, mhpp, parse_args)
    else:
        cpp, res = _tx.transpile_impl(rel, repo, xpp, mhpp, pr)
    return rel, (str(cpp) if cpp else None), res


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
        # Incremental state: previous run's hashes/deps vs this run's.
        self._cache_path = store.root / "xbuild-cache.json"
        self.prev = self._load_cache()
        self.cur = {"files": {}, "objs": {}}
        self._sha_cache: dict[str, str] = {}

    def _load_cache(self) -> dict:
        import json
        if self._cache_path.exists():
            try:
                return json.loads(self._cache_path.read_text(encoding="utf-8"))
            except Exception:
                pass
        return {"files": {}, "objs": {}}

    def _save_cache(self) -> None:
        import json
        self._cache_path.parent.mkdir(parents=True, exist_ok=True)
        self._cache_path.write_text(json.dumps(self.cur), encoding="utf-8")

    def _sha(self, path) -> str:
        import hashlib
        key = str(path)
        if key in self._sha_cache:
            return self._sha_cache[key]
        try:
            h = hashlib.sha256(Path(path).read_bytes()).hexdigest()[:16]
        except OSError:
            h = ""
        self._sha_cache[key] = h
        return h

    def _flags_sig(self, t: dict) -> str:
        import hashlib
        return hashlib.sha256(("\0".join(self._base_flags(t))).encode()).hexdigest()[:16]

    def _cpp_path_for(self, rel: str) -> Path:
        if rel.endswith(".ixx"):
            return tx.cpp_path(self.xpp, self.provides.get(rel), Path(rel).stem)
        return tx.impl_path(self.xpp, rel)

    @staticmethod
    def _parse_depfile(path: Path) -> list[str]:
        """Return the header paths a .d (makefile-style) depfile lists."""
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            return []
        text = text.replace("\\\n", " ").replace("\\\r\n", " ")
        if ":" in text:
            text = text.split(":", 1)[1]
        return [tok for tok in text.split() if tok not in ("\\",)]

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
        mhpp_str = {k: str(v) for k, v in mhpp.items()}
        total = len(self.rels)
        extra = self.manifest.get("comprehend", {}).get("extra_args", [])

        # PROJECT_ROOT (for resolving non-"./" resource paths) from defines.
        project_root = ""
        for t in self.manifest["targets"]:
            for d in t.get("defines", []):
                if d.startswith("PROJECT_ROOT="):
                    project_root = d.split("=", 1)[1].strip('"')

        # Decide per source: re-transpile only if its content changed (L1), the
        # parse flags changed, or an EMBEDDED RESOURCE it references changed --
        # otherwise reuse the existing .hpp/.cpp.
        import hashlib
        tasks = []
        meta: dict[str, tuple[str, str]] = {}  # rel -> (src_sha, pa_sig)
        reused = 0
        for rel in self.rels:
            self.cpp_of[rel] = self._cpp_path_for(rel)
            src_sha = self._sha(self.repo / rel)
            pa = (self._parse_args(self.target_of[rel]) + extra) if rel.endswith(".ixx") else []
            pa_sig = hashlib.sha256("\0".join(pa).encode()).hexdigest()[:16]
            meta[rel] = (src_sha, pa_sig)
            prev = self.prev["files"].get(rel)
            cpp = self.cpp_of[rel]
            hpp = cpp.with_suffix(".hpp")
            res_ok = prev and all(self._sha(p) == s
                                  for p, s in prev.get("res_deps", {}).items())
            if (prev and prev.get("src_sha") == src_sha and prev.get("pa") == pa_sig
                    and res_ok and cpp.exists()
                    and (not rel.endswith(".ixx") or hpp.exists())):
                self.cur["files"][rel] = prev  # unchanged: reuse artifacts + hashes
                reused += 1
                continue
            tasks.append((rel, str(self.repo), str(self.xpp), mhpp_str, pa, project_root))

        print(f"[1/3] Transpiling: {len(tasks)} changed, {reused} reused (-j{self.jobs})...")

        def consume(triples):
            for i, (rel, cpp, res) in enumerate(triples, 1):
                print(f"  [{i:>3}/{len(tasks)}] XPP {rel}")
                # The files were just rewritten -> drop any cached hashes.
                self._sha_cache.pop(str(self.cpp_of[rel]), None)
                hpp = self.cpp_of[rel].with_suffix(".hpp") if rel.endswith(".ixx") else None
                if hpp:
                    self._sha_cache.pop(str(hpp), None)
                src_sha, pa_sig = meta[rel]
                self.cur["files"][rel] = {
                    "src_sha": src_sha, "pa": pa_sig,
                    "hpp_sha": self._sha(hpp) if hpp else "",
                    "cpp_sha": self._sha(self.cpp_of[rel]),
                    "res_deps": {p: self._sha(p) for p in (res or [])},
                }

        # Transpiling is GIL-bound (Python AST walk) -> PROCESSES for parallelism.
        if not tasks:
            pass
        elif self.jobs == 1:
            consume(_transpile_worker(t) for t in tasks)
        else:
            with concurrent.futures.ProcessPoolExecutor(max_workers=self.jobs) as ex:
                consume(ex.map(_transpile_worker, tasks))
        print(f"      transpiled into {self.xpp}")

    # -- stage: classic compile -------------------------------------------

    def _obj_dirty(self, rel: str, flags_sig: str) -> bool:
        """A .obj must be rebuilt if it's missing, its own .cpp changed, the
        flags changed, or any header in its recorded depfile changed."""
        obj = self._obj(rel)
        if not obj.exists():
            return True
        prev = self.prev["objs"].get(rel)
        if not prev or prev.get("flags") != flags_sig:
            return True
        if prev.get("cpp_sha") != self.cur["files"].get(rel, {}).get("cpp_sha"):
            return True
        for dep, sha in prev.get("deps", {}).items():
            if self._sha(dep) != sha:   # a #included header changed
                return True
        return False

    def _compile_classic(self, rel: str, flags_sig: str, total: int = 0) -> bool:
        t = self.target_of[rel]
        cpp = self.cpp_of.get(rel)
        if cpp is None:
            return True
        obj = self._obj(rel)
        obj.parent.mkdir(parents=True, exist_ok=True)
        dep = Path(str(obj) + ".d")
        cmd = [self.cxx, *self._base_flags(t), "-x", "c++", "-MMD", "-MF", str(dep),
               "-c", str(cpp), "-o", str(obj)]
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
        deps = {d: self._sha(d) for d in self._parse_depfile(dep)}
        with self._lock:
            self.compiled.add(rel)
            self.cur["objs"][rel] = {
                "cpp_sha": self.cur["files"].get(rel, {}).get("cpp_sha"),
                "flags": flags_sig, "deps": deps,
            }
        return True

    # -- orchestration -----------------------------------------------------

    def build(self) -> bool:
        self.transpile_all()
        total = len(self.rels)
        self._n = 0
        flags_sig = {t["name"]: self._flags_sig(t) for t in self.manifest["targets"]}

        # Decide which objects are actually dirty (content + header propagation).
        todo = [r for r in self.rels if self._obj_dirty(r, flags_sig[self.target_of[r]["name"]])]
        # Carry forward cache for objects we are NOT rebuilding.
        for r in self.rels:
            if r not in todo and r in self.prev["objs"]:
                self.cur["objs"][r] = self.prev["objs"][r]
        print(f"[2/3] Compiling: {len(todo)} dirty, {len(self.rels) - len(todo)} cached (-j{self.jobs})...")
        results = self._run_pool(
            todo, lambda r: self._compile_classic(r, flags_sig[self.target_of[r]["name"]], len(todo)))
        if not all(results):
            self._save_cache()
            return False

        # Link a target if any of its objects was (re)built, its output is
        # missing, or a static dependency relinked.
        print("[3/3] Linking...")
        recompiled = set(self.compiled)
        relinked = set()
        for t in sorted(self.manifest["targets"], key=lambda x: 0 if x["kind"] == "static" else 1):
            out = self._target_output(t)
            touched = any(r in recompiled for r in t["sources"])
            dep_relinked = any(d in relinked for d in t.get("depends", []))
            if not (touched or dep_relinked or not out.exists()):
                continue
            ok = self._link_static(t) if t["kind"] == "static" else self._link_exe(t)
            if not ok:
                self._save_cache()
                return False
            relinked.add(t["name"])
        self.__dict__["_relinked"] = relinked
        self._save_cache()
        return True
