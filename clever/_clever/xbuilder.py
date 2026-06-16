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
        _, cpp, res, syms = _tx.transpile(rel, repo, xpp, mhpp, parse_args)
    else:
        cpp, res, syms = _tx.transpile_impl(rel, repo, xpp, mhpp, pr)
    return rel, (str(cpp) if cpp else None), res, syms


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
    # (WIN32_LEAN_AND_MEAN is deliberately NOT set globally: it strips the OLE /
    # shell pieces of windows.h that CAM's GUI needs -- LPMSG, DLGPROC, etc.
    # The winsock1-vs-winsock2 clash it guarded against is confined to Rev's
    # socket TUs, which include <winsock2.h> before any windows.h themselves.)
    _LEAK_GUARD = ["-DNOMINMAX"]

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
            # STRUCTURAL mtime guard (orthogonal to the content hash): if the
            # source is newer on disk than the artifacts it should have produced,
            # ALWAYS re-transpile -- even if src_sha "matches". This closes the
            # race where a file edited *during* a build leaves the cache recording
            # a sha for artifacts that were actually generated from older bytes.
            arts = [cpp] + ([hpp] if rel.endswith(".ixx") else [])
            src_mtime = self._mtime(self.repo / rel)
            mtime_ok = all(a.exists() and src_mtime <= self._mtime(a) for a in arts)
            if (prev and prev.get("src_sha") == src_sha and prev.get("pa") == pa_sig
                    and res_ok and cpp.exists()
                    and (not rel.endswith(".ixx") or hpp.exists())
                    and mtime_ok):
                self.cur["files"][rel] = prev  # unchanged: reuse artifacts + hashes
                reused += 1
                continue
            tasks.append((rel, str(self.repo), str(self.xpp), mhpp_str, pa, project_root))

        print(f"[1/3] Transpiling: {len(tasks)} changed, {reused} reused (-j{self.jobs})...")

        def consume(quads):
            for i, (rel, cpp, res, syms) in enumerate(quads, 1):
                print(f"  [{i:>3}/{len(tasks)}] XPP {rel}")
                # The files were just rewritten -> drop any cached hashes.
                self._sha_cache.pop(str(self.cpp_of[rel]), None)
                hpp = self.cpp_of[rel].with_suffix(".hpp") if rel.endswith(".ixx") else None
                if hpp:
                    self._sha_cache.pop(str(hpp), None)
                src_sha, pa_sig = meta[rel]
                self.cur["files"][rel] = {
                    "src_sha": src_sha, "pa": pa_sig, "symbols": syms or [],
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
        self._qualify_collisions([t[0] for t in tasks])

    def _qualify_collisions(self, retranspiled: list[str]) -> None:
        """Pre-disambiguate names defined in >1 namespace: in each (re)transpiled
        file, rewrite unqualified collision-name uses to the namespace that file's
        own module / imports actually provide. This neutralises the leaked
        `using namespace` directives that headers (unlike modules) propagate."""
        # Collision map (name -> {namespaces}) and module -> [[ns,name]] from ALL files.
        name_ns: dict[str, set] = {}
        mod_syms: dict[str, list] = {}
        for rel, fc in self.cur["files"].items():
            mod = self.provides.get(rel)
            syms = fc.get("symbols", [])
            if mod:
                mod_syms[mod] = syms
            for ns, name in syms:
                name_ns.setdefault(name, set()).add(ns)
        collisions = {n for n, nss in name_ns.items() if len(nss) > 1}
        if not collisions or not retranspiled:
            return
        n_qual = 0
        for rel in retranspiled:
            # NEVER qualify a name this file itself defines (it's unambiguous
            # there, and qualifying would break ctor/dtor/out-of-line syntax).
            own = {name for _, name in mod_syms.get(self.provides.get(rel), [])}
            # Qualify imported collision names: target = the namespace of the
            # single import that provides it (skip if two imports conflict).
            n2t: dict[str, str] = {}
            conflict: set = set()
            for M in self.requires.get(rel, []):
                for ns, name in mod_syms.get(M, []):
                    if name not in collisions or name in own:
                        continue
                    if name in n2t and n2t[name] != ns:
                        conflict.add(name)
                    else:
                        n2t.setdefault(name, ns)
            for name in conflict:
                n2t.pop(name, None)
            if not n2t:
                continue
            for path in ({self.cpp_of[rel], self.cpp_of[rel].with_suffix(".hpp")}
                         if rel.endswith(".ixx") else {self.cpp_of[rel]}):
                if not path.exists():
                    continue
                text = path.read_bytes().decode("latin-1")
                new = tx.qualify_text(text, n2t)
                if new != text:
                    path.write_bytes(new.encode("latin-1"))
                    self._sha_cache.pop(str(path), None)
                    n_qual += 1
            # refresh recorded hashes (qualify rewrote the files)
            fc = self.cur["files"][rel]
            hpp = self.cpp_of[rel].with_suffix(".hpp") if rel.endswith(".ixx") else None
            fc["hpp_sha"] = self._sha(hpp) if hpp else ""
            fc["cpp_sha"] = self._sha(self.cpp_of[rel])
        if n_qual:
            print(f"      qualified {len(collisions)} colliding name(s) in {n_qual} file(s)")

    # -- stage: classic compile -------------------------------------------

    def _obj_dirty(self, rel: str, flags_sig: str) -> bool:
        """A .obj must be rebuilt if it's missing, its own .cpp changed, the
        flags changed, any header in its recorded depfile changed, OR (structural
        guard) any input is newer on disk than the .obj -- the latter catches an
        input edited *during* a build that the content hash might otherwise have
        recorded as already-built."""
        obj = self._obj(rel)
        if not obj.exists():
            return True
        prev = self.prev["objs"].get(rel)
        if not prev or prev.get("flags") != flags_sig:
            return True
        if prev.get("cpp_sha") != self.cur["files"].get(rel, {}).get("cpp_sha"):
            return True
        obj_mtime = self._mtime(obj)
        cpp = self.cpp_of.get(rel)
        if cpp is not None and self._mtime(cpp) > obj_mtime:   # .cpp newer than .obj
            return True
        for dep, sha in prev.get("deps", {}).items():
            if self._sha(dep) != sha:           # a #included header changed
                return True
            if self._mtime(dep) > obj_mtime:    # .hpp/header newer than .obj
                return True
        return False

    @staticmethod
    def _mtime(p) -> float:
        """Modification time, or -inf if the path is missing (so a missing input
        never reads as 'newer' and a missing artifact always reads as 'older')."""
        try:
            return Path(p).stat().st_mtime
        except OSError:
            return float("-inf")

    # Module imports are opaque BMIs, so in module-world no single TU ever saw
    # both <winsock2.h> (Rev's sockets) and the OLE/shell half of <windows.h>
    # (OpenCASCADE pulls shlobj/ole2 transitively). Transpiling flattens every
    # imported module's global-fragment includes into one TU, resurfacing the
    # classic winsock ordering clash. Force <winsock2.h> in first, before any
    # header in the TU: that establishes winsock2 (no winsock1 redefinitions)
    # AND pulls the full windows.h (so LPMSG/DLGPROC exist for later ole2.h).
    # commctrl.h likewise: windows.h does not include it, but shlobj/shobjidl
    # (propagated into consumers via File.win.hpp) need HIMAGELIST/TBBUTTON from
    # it. In module-world each module's global fragment arranged this ordering;
    # flattening loses it, so we restore a small curated prelude here.
    _FORCE_INC = ["-include", "winsock2.h", "-include", "windows.h",
                  "-include", "commctrl.h"]

    def _compile_classic(self, rel: str, flags_sig: str, total: int = 0) -> bool:
        t = self.target_of[rel]
        cpp = self.cpp_of.get(rel)
        if cpp is None:
            return True
        obj = self._obj(rel)
        obj.parent.mkdir(parents=True, exist_ok=True)
        dep = Path(str(obj) + ".d")
        cmd = [self.cxx, *self._base_flags(t), *self._FORCE_INC,
               "-x", "c++", "-MMD", "-MF", str(dep),
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
        def fsig(r: str) -> str:
            return flags_sig[self.target_of[r]["name"]] + "|" + "".join(self._FORCE_INC)
        todo = [r for r in self.rels if self._obj_dirty(r, fsig(r))]
        # Carry forward cache for objects we are NOT rebuilding.
        for r in self.rels:
            if r not in todo and r in self.prev["objs"]:
                self.cur["objs"][r] = self.prev["objs"][r]
        print(f"[2/3] Compiling: {len(todo)} dirty, {len(self.rels) - len(todo)} cached (-j{self.jobs})...")
        results = self._run_pool(
            todo, lambda r: self._compile_classic(r, fsig(r), len(todo)))
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
