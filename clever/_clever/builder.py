"""The build driver: turn the dirty set into real compile/link/run actions.

Compilation order matters for C++ modules: a module interface must be compiled
(producing its BMI / `.pcm`) before anything that imports it. So we walk the
import graph depth-first from the leaves up -- providers before consumers --
and compile a translation unit when either its object is missing or the ladder
flagged it dirty. Each unit is given `-fmodule-file=` for the full transitive
closure of the modules it imports.

Outputs live under `.clever/out/`:
  bmi/<Module>.pcm        module interfaces
  obj/<target>/<rel>.obj  objects
  <target output>         Rev.lib / CAMDemo.exe

Static libraries are archived with llvm-ar; the executable is linked with clang
driving lld-link, reusing the exact link libraries / flags harvested into
clever.json, then the vcpkg DLL-copy post-build step is replayed at our exe.
"""

from __future__ import annotations

import subprocess
from collections import deque
from pathlib import Path


class Builder:
    def __init__(self, manifest: dict, repo: Path, store, rebuild_set: set[str],
                 digests: dict | None = None, verbose=False):
        self.manifest = manifest
        self.repo = repo
        self.store = store
        self.rebuild_set = rebuild_set
        self.verbose = verbose
        # `digests` are this run's freshly computed digests (module/imports up to
        # date). We must NOT read them from the store, whose copy reflects the
        # last successful build and can be stale for files edited since.
        self._digests = digests or {}
        self.out = store.root / "out"
        self.cmake_build = repo / manifest.get("cmake_build_dir", "build")
        c = manifest["compiler"]
        self.cxx = c["cxx"]
        self.ar = c["ar"]
        self.ranlib = c["ranlib"]
        self.std = c["std"]

        # Per-file target + module graph, read from the staged digests.
        self.target_of: dict[str, dict] = {}
        for t in manifest["targets"]:
            for s in t["sources"]:
                self.target_of.setdefault(s, t)
        self.rels = [r for r in self.target_of if (repo / r).exists()]

        self.provides: dict[str, str] = {}      # rel -> module
        self.requires: dict[str, list[str]] = {}  # rel -> [module]
        self.provider: dict[str, str] = {}      # module -> rel
        for rel in self.rels:
            d = self._digests.get(rel) or store.read(rel, "digest") or {}
            mod = d.get("provides")
            self.provides[rel] = mod
            self.requires[rel] = d.get("requires", [])
            if mod:
                self.provider[mod] = rel

        self.compiled: set[str] = set()
        self.failed = False

    # -- paths -------------------------------------------------------------

    def _obj(self, rel: str) -> Path:
        target = self.target_of[rel]["name"]
        return self.out / "obj" / target / (rel + ".obj")

    def _bmi(self, module: str) -> Path:
        return self.out / "bmi" / (module + ".pcm")

    def _target_output(self, t: dict) -> Path:
        return self.out / t["output"].replace("\\", "/")

    # -- graph -------------------------------------------------------------

    def _closure(self, rel: str) -> list[str]:
        seen: set[str] = set()
        stack = list(self.requires.get(rel, []))
        while stack:
            m = stack.pop()
            if m in seen:
                continue
            seen.add(m)
            prov = self.provider.get(m)
            if prov:
                stack.extend(self.requires.get(prov, []))
        return sorted(seen)

    def _topo(self) -> list[str]:
        order: list[str] = []
        state: dict[str, int] = {}

        def visit(rel, chain):
            st = state.get(rel)
            if st == 1:
                return
            if st == 0:
                raise RuntimeError("import cycle: " + " -> ".join(chain))
            state[rel] = 0
            for m in self.requires.get(rel, []):
                prov = self.provider.get(m)
                if prov and prov != rel:
                    visit(prov, chain + [m])
            state[rel] = 1
            order.append(rel)

        for rel in self.rels:
            visit(rel, [self.provides.get(rel) or rel])
        return order

    # -- compile -----------------------------------------------------------

    def _base_flags(self, t: dict) -> list[str]:
        fl = []
        for d in t.get("defines", []):
            fl.append("-D" + d)
        for inc in t.get("include_dirs", []):
            fl.append("-I" + str(self.repo / inc))
        fl += t.get("compile_flags", [])
        fl.append("-std=" + self.std)
        return fl

    def _needs(self, rel: str) -> bool:
        return rel in self.rebuild_set or not self._obj(rel).exists()

    def _compile(self, rel: str, label: str | None = None) -> tuple[bool, str]:
        t = self.target_of[rel]
        obj = self._obj(rel)
        obj.parent.mkdir(parents=True, exist_ok=True)
        is_module = rel.endswith(".ixx")
        mod = self.provides.get(rel)

        cmd = [self.cxx, *self._base_flags(t)]
        if is_module and mod:
            bmi = self._bmi(mod)
            bmi.parent.mkdir(parents=True, exist_ok=True)
            cmd += ["-x", "c++-module", f"-fmodule-output={bmi}"]
        for m in self._closure(rel):
            if m in self.provider:
                cmd.append(f"-fmodule-file={m}={self._bmi(m)}")
        cmd += ["-o", str(obj), "-c", str(self.repo / rel)]

        print(f"  CXX  {label or (t['name'] + '/' + Path(rel).name)}")
        if self.verbose:
            print("       " + " ".join(cmd))
        p = subprocess.run(cmd, capture_output=True, text=True, cwd=str(self.repo))
        if p.returncode != 0:
            return False, (p.stdout + "\n" + p.stderr).strip()
        if p.stderr.strip():
            print(p.stderr)
        self.compiled.add(rel)
        return True, ""

    # -- staleness repair (clang-as-canary recovery) -----------------------

    def _mtime(self, path: Path) -> float:
        try:
            return path.stat().st_mtime
        except OSError:
            return -1.0

    def _bmi_stale(self, rel: str) -> bool:
        """A module's BMI is stale if it is missing, older than its own source,
        or older than any BMI it directly imports (mtime is a conservative
        proxy -- worst case we rebuild a BMI whose content was unchanged)."""
        mod = self.provides.get(rel)
        if not rel.endswith(".ixx") or not mod:
            return False
        pcm = self._bmi(mod)
        if not pcm.exists():
            return True
        pm = self._mtime(pcm)
        if pm < self._mtime(self.repo / rel):
            return True
        for m in self.requires.get(rel, []):
            prov = self.provider.get(m)
            if prov and self._mtime(self._bmi(m)) > pm:
                return True
        return False

    def _closure_rels(self, rel: str) -> list[str]:
        """Provider source files for `rel`'s transitive module imports, in
        topological (dependency-first) order."""
        wanted = {self.provider[m] for m in self._closure(rel) if m in self.provider}
        return [r for r in self._topo() if r in wanted]

    def _repair_closure(self, rel: str, full: bool) -> list[str]:
        """Rebuild stale (or, if full, all) BMIs in rel's import closure,
        dependency-first so freshly-bumped BMIs cascade staleness outward."""
        rebuilt = []
        for dep in self._closure_rels(rel):
            if full or self._bmi_stale(dep):
                ok, out = self._compile(dep, label=f"(repair) {self.target_of[dep]['name']}/{Path(dep).name}")
                if ok:
                    rebuilt.append(dep)
                else:
                    print(f"  repair of {dep} failed:\n{out}")
        return rebuilt

    def _attempt(self, rel: str) -> bool:
        """Compile rel, recovering from stale-BMI failures with escalating,
        closure-scoped rebuilds -- using clang as the canary."""
        ok, out = self._compile(rel)
        if ok:
            return True

        # Mini nuke: rebuild only the BMIs in this TU's closure that look stale.
        rebuilt = self._repair_closure(rel, full=False)
        if rebuilt:
            print(f"  ...repaired {len(rebuilt)} stale BMI(s); retrying {Path(rel).name}")
            ok, out = self._compile(rel)
            if ok:
                return True

        # Medium nuke: rebuild this TU's entire import closure (content staleness
        # clang sees that mtime did not). Still scoped to deps, never the project.
        print(f"  ...escalating: rebuilding full import closure of {Path(rel).name}")
        self._repair_closure(rel, full=True)
        ok, out = self._compile(rel)
        if ok:
            return True

        # A clean closure that still fails is a genuine compile error, not
        # staleness -- report it; do NOT nuke the whole project for a real bug.
        print(f"FAILED: {rel}\n{out}")
        self.failed = True
        return False

    # -- link --------------------------------------------------------------

    def _link_static(self, t: dict) -> bool:
        out = self._target_output(t)
        out.parent.mkdir(parents=True, exist_ok=True)
        objs = [str(self._obj(r)) for r in t["sources"] if (r in self.rels and self._obj(r).exists())]
        print(f"  AR   {t['output']}")
        if out.exists():
            out.unlink()
        p = subprocess.run([self.ar, "qc", str(out), *objs], capture_output=True, text=True)
        if p.returncode != 0:
            print(f"FAILED ar {t['name']}\n{p.stderr}")
            return False
        subprocess.run([self.ranlib, str(out)], capture_output=True, text=True)
        return True

    def _remap_lib(self, lib: str) -> str:
        lib = lib.replace("\\", "/")
        if lib.startswith("-l") or lib.startswith("/") or ":" in lib.split("/")[0]:
            return lib  # system lib or absolute path
        return str(self.cmake_build / lib)  # relative to cmake build dir (vcpkg)

    def _link_exe(self, t: dict) -> bool:
        out = self._target_output(t)
        out.parent.mkdir(parents=True, exist_ok=True)
        objs = [str(self._obj(r)) for r in t["sources"] if (r in self.rels and self._obj(r).exists())]

        rsp_lines = list(objs)
        for dep in t.get("depends", []):
            dep_t = next((x for x in self.manifest["targets"] if x["name"] == dep), None)
            if dep_t:
                rsp_lines.append(str(self._target_output(dep_t)))
        for lib in t.get("link_libraries", []):
            rsp_lines.append(self._remap_lib(lib))
        # clang/lld read response files with GNU quoting, where a backslash is
        # an escape character -- so Windows paths MUST use forward slashes here.
        rsp = self.out / (t["name"] + ".link.rsp")
        rsp.write_text("\n".join(s.replace("\\", "/") for s in rsp_lines), encoding="utf-8")

        cmd = [self.cxx, "-nostartfiles", "-nostdlib",
               *t.get("compile_flags", []), *t.get("link_flags", []),
               f"@{rsp}", "-o", str(out), "-Xlinker", "/MANIFEST:EMBED"]
        if t.get("implib"):
            cmd += ["-Xlinker", f"/implib:{self.out / t['implib'].replace(chr(92), '/')}"]
        if t.get("pdb"):
            cmd += ["-Xlinker", f"/pdb:{self.out / t['pdb'].replace(chr(92), '/')}"]
        cmd += ["-Xlinker", "/version:0.0"]

        print(f"  LINK {t['output']}")
        if self.verbose:
            print("       " + " ".join(cmd))
        p = subprocess.run(cmd, capture_output=True, text=True, cwd=str(self.cmake_build))
        if p.stdout.strip():
            print(p.stdout)
        if p.returncode != 0:
            print(f"FAILED link {t['name']}\n{p.stderr}")
            return False
        self._post_build(t, out)
        return True

    def _post_build(self, t: dict, out: Path) -> None:
        """Make the exe runnable in place by copying its dependency DLLs next to
        it (the cmake build used vcpkg's applocal.ps1 for this; we just copy the
        vcpkg runtime DLLs, which is simpler and reliable)."""
        import shutil
        bin_dirs: set[Path] = set()
        for lib in t.get("link_libraries", []):
            p = lib.replace("\\", "/")
            if "vcpkg_installed" in p and "/lib/" in p:
                rel_bin = p[: p.index("/lib/")] + "/bin"
                bin_dirs.add(self.cmake_build / rel_bin)
        copied = 0
        for d in bin_dirs:
            if not d.is_dir():
                continue
            for dll in d.glob("*.dll"):
                dest = out.parent / dll.name
                if not dest.exists() or dest.stat().st_mtime < dll.stat().st_mtime:
                    shutil.copy2(dll, dest)
                    copied += 1
        if copied:
            print(f"       copied {copied} dependency DLL(s) next to {out.name}")

    # -- orchestration -----------------------------------------------------

    def plan(self) -> dict:
        """What build() would do, without doing it."""
        order = self._topo()
        todo = [r for r in order if self._needs(r)]
        todo_set = set(todo)
        link = []
        for t in self.manifest["targets"]:
            out = self._target_output(t)
            touched = any(r in todo_set for r in t["sources"])
            if touched or not out.exists():
                link.append(t["name"])
        return {
            "compile": [(r, r not in self.rebuild_set) for r in todo],  # (rel, only-because-missing)
            "link": link,
        }

    def build(self) -> bool:
        order = self._topo()
        todo = [r for r in order if self._needs(r)]
        print(f"Compiling {len(todo)} of {len(order)} translation units...")
        for rel in todo:
            if self.failed:
                return False
            if not self._attempt(rel):
                return False

        # Link any target that had an object (re)compiled, or whose output is missing.
        for t in self.manifest["targets"]:
            out = self._target_output(t)
            touched = any(r in self.compiled for r in t["sources"])
            dep_relinked = any(
                self._target_output(next(x for x in self.manifest["targets"] if x["name"] == d)).exists()
                and d in getattr(self, "_relinked", set())
                for d in t.get("depends", [])
            )
            if not (touched or dep_relinked or not out.exists()):
                continue
            ok = self._link_static(t) if t["kind"] == "static" else self._link_exe(t)
            if not ok:
                return False
            self.__dict__.setdefault("_relinked", set()).add(t["name"])
        return True

    def run(self, target_name: str | None = None) -> int:
        exes = [t for t in self.manifest["targets"] if t["kind"] == "exe"]
        if target_name:
            exes = [t for t in exes if t["name"] == target_name]
        if not exes:
            print("No executable target to run.")
            return 1
        t = exes[0]
        out = self._target_output(t)
        print(f"\nRunning {out} ...")
        return subprocess.run([str(out)], cwd=str(out.parent)).returncode
