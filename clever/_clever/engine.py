"""The incremental build engine.

This is where clever earns its name. Two mechanisms work together to stop the
whole-project rebuild cascades that plague the naive ninja+modules setup:

1. Normalised-source gating (in scan/cppstrip): a comment- or whitespace-only
   edit produces an identical canonical source hash, so the file is not even
   recompiled -- and therefore cannot disturb anything downstream.

2. BMI content-hash gating (here): when a file *is* recompiled, we hash the
   `.pcm` (binary module interface) it produces. A consumer is only considered
   dirty if a module it imports has a *different* BMI than the one present when
   the consumer was last built. So an implementation-detail change that happens
   to leave the serialised interface byte-identical does not propagate.

Together these mean: change a comment in a widely-imported module -> nothing
rebuilds; change a private detail that doesn't alter the emitted BMI -> only
that one file rebuilds; change an exported signature -> exactly the importers
that needed it rebuild.
"""

from __future__ import annotations

import concurrent.futures
import hashlib
import subprocess
import threading
from dataclasses import dataclass, field
from pathlib import Path

from .config import Project, Unit
from .graph import Graph, build_graph
from .scan import Scanner


def _sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def _flags_hash(unit: Unit, closure: list[str]) -> str:
    payload = "\0".join(unit.flags) + "\0#mod=" + str(unit.is_module) + "\0#clo=" + ",".join(closure)
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


@dataclass
class Plan:
    obj_of: dict[Path, Path]
    pcm_of: dict[str, Path]
    order_ready: dict[Path, list[Path]]  # node -> direct provider-file deps in set


class Engine:
    def __init__(self, project: Project, cache, build_dir: Path, jobs: int = 1, verbose: bool = False):
        self.project = project
        self.cache = cache
        self.build_dir = build_dir
        self.obj_dir = build_dir / "obj"
        self.bmi_dir = build_dir / "bmi"
        self.jobs = max(1, jobs)
        self.verbose = verbose
        self.lock = threading.Lock()
        self.graph: Graph | None = None
        self.rebuilt_targets: set[str] = set()
        self.compiled: set[Path] = set()
        self.failed = False

    # -- planning ----------------------------------------------------------

    def scan_all(self) -> Graph:
        scanner = Scanner(self.project.compiler, self.cache.scan)
        scans = {}
        units_meta = []
        # Deduplicate sources for scanning (a source compiled into two targets
        # has identical module semantics); use the first unit's flags.
        seen_src: dict[Path, Unit] = {}
        for u in self.project.units:
            seen_src.setdefault(u.source, u)
        for src, u in seen_src.items():
            if not src.exists():
                continue
            scans[src] = scanner.scan(u.flags, src)
        for u in self.project.units:
            if u.source in scans:
                units_meta.append((u.source, u.target, u.is_module))
        self.graph = build_graph(units_meta, scans)
        return self.graph

    def _obj_path(self, unit: Unit) -> Path:
        repo = self.project.cmake_build_dir.parent
        try:
            rel = unit.source.resolve().relative_to(repo.resolve())
            flat = str(rel).replace("\\", "/")
        except ValueError:
            flat = unit.source.name
        return self.obj_dir / unit.target / (flat + ".obj")

    def _pcm_path(self, module_name: str) -> Path:
        return self.bmi_dir / (module_name + ".pcm")

    # -- compilation -------------------------------------------------------

    def _compile_cmd(self, unit: Unit) -> list[str]:
        g = self.graph
        node = g.nodes[unit.source]
        closure = g.closure(unit.source)
        obj = self._obj_path(unit)
        obj.parent.mkdir(parents=True, exist_ok=True)

        cmd = [self.project.compiler, *unit.flags]
        if unit.is_module:
            pcm = self._pcm_path(node.provides) if node.provides else None
            cmd += ["-x", "c++-module"]
            if pcm is not None:
                pcm.parent.mkdir(parents=True, exist_ok=True)
                cmd += [f"-fmodule-output={pcm}"]
        for name in closure:
            prov = g.provider.get(name)
            if prov is not None:
                cmd += [f"-fmodule-file={name}={self._pcm_path(name)}"]
        cmd += ["-o", str(obj), "-c", str(unit.source)]
        return cmd

    def _needs_compile(self, unit: Unit) -> tuple[bool, str, str, list[str]]:
        g = self.graph
        node = g.nodes[unit.source]
        closure = g.closure(unit.source)
        nh = self.cache.scan.get(str(unit.source), {}).get("norm_hash", "")
        fh = _flags_hash(unit, closure)
        obj = self._obj_path(unit)

        if not obj.exists():
            return True, nh, fh, closure
        if unit.is_module and node.provides and not self._pcm_path(node.provides).exists():
            return True, nh, fh, closure

        rec = self.cache.files.get(self._file_key(unit))
        if not rec:
            return True, nh, fh, closure
        if rec.get("norm_hash") != nh or rec.get("flags_hash") != fh:
            return True, nh, fh, closure

        # BMI gating: did any imported module's interface change since we built?
        recorded = rec.get("dep_bmis", {})
        for name in closure:
            if self.cache.bmi.get(name) != recorded.get(name):
                return True, nh, fh, closure
        return False, nh, fh, closure

    def _file_key(self, unit: Unit) -> str:
        return f"{unit.target}::{unit.source}"

    def _run_unit(self, unit: Unit) -> None:
        if self.failed:
            return
        needs, nh, fh, closure = self._needs_compile(unit)
        node = self.graph.nodes[unit.source]
        if needs:
            cmd = self._compile_cmd(unit)
            label = f"{unit.target}/{unit.source.name}"
            print(f"  CXX  {label}")
            if self.verbose:
                print("       " + " ".join(cmd))
            proc = subprocess.run(cmd, capture_output=True, text=True,
                                  cwd=str(self.project.cmake_build_dir.parent))
            if proc.stdout.strip():
                print(proc.stdout)
            if proc.returncode != 0:
                print(f"FAILED: {label}\n{proc.stderr}")
                self.failed = True
                return
            if proc.stderr.strip():
                print(proc.stderr)
            with self.lock:
                self.compiled.add(self._obj_path(unit))
                if unit.is_module and node.provides:
                    self.cache.bmi[node.provides] = _sha256_file(self._pcm_path(node.provides))
        # Record state (dep_bmis snapshot reflects current, post-dep BMIs).
        with self.lock:
            self.cache.files[self._file_key(unit)] = {
                "norm_hash": nh,
                "flags_hash": fh,
                "provides": node.provides,
                "dep_bmis": {name: self.cache.bmi.get(name) for name in closure},
            }

    # -- scheduling --------------------------------------------------------

    def compile_targets(self, target_names: list[str]) -> bool:
        g = self.graph
        # Units we must build = all units belonging to requested targets plus
        # any library targets they depend on (handled by including all units of
        # every target whose module/objects feed the request). Simplest correct
        # choice: build every unit, since cross-target module imports are common.
        wanted = set(target_names)
        # include library deps referenced via link_libraries like "Rev/Rev.lib"
        for tname in list(target_names):
            t = self.project.targets.get(tname)
            if not t:
                continue
            for lib in t.link_libraries:
                stem = Path(lib.replace("\\", "/")).stem
                if stem in self.project.targets:
                    wanted.add(stem)

        units = [u for u in self.project.units if u.target in wanted and u.source in g.nodes]

        # Build readiness: a unit depends on the provider-files of the modules
        # it imports (direct), restricted to units in our set.
        unit_by_src_target = {(u.source, u.target): u for u in units}
        in_set_src = {u.source for u in units}

        deps: dict[tuple, set] = {}
        for u in units:
            d = set()
            for name in g.nodes[u.source].requires:
                prov = g.provider.get(name)
                if prov is not None and prov in in_set_src and prov != u.source:
                    # depend on the provider in the *same* set; module objects
                    # live in their owning target, so match by provider source.
                    for cand in units:
                        if cand.source == prov:
                            d.add((cand.source, cand.target))
            deps[(u.source, u.target)] = d

        done: set[tuple] = set()
        keys = list(unit_by_src_target.keys())

        if self.jobs == 1:
            # Deterministic topo single-thread.
            ordered = self._toposort(keys, deps)
            for k in ordered:
                if self.failed:
                    break
                self._run_unit(unit_by_src_target[k])
            return not self.failed

        # Parallel: dispatch ready nodes.
        with concurrent.futures.ThreadPoolExecutor(max_workers=self.jobs) as ex:
            remaining = set(keys)
            futures: dict = {}
            while remaining and not self.failed:
                ready = [k for k in remaining if deps[k] <= done and k not in futures]
                if not ready and not futures:
                    raise RuntimeError("Dependency deadlock / cycle in build set")
                for k in ready:
                    futures[k] = ex.submit(self._run_unit, unit_by_src_target[k])
                # Wait for at least one to finish.
                doneset, _ = concurrent.futures.wait(
                    futures.values(), return_when=concurrent.futures.FIRST_COMPLETED
                )
                for k in list(futures.keys()):
                    if futures[k] in doneset:
                        futures[k].result()
                        done.add(k)
                        remaining.discard(k)
                        del futures[k]
        return not self.failed

    @staticmethod
    def _toposort(keys, deps) -> list:
        order = []
        temp, perm = set(), set()

        def visit(k):
            if k in perm:
                return
            temp.add(k)
            for d in deps[k]:
                if d not in perm:
                    visit(d)
            perm.add(k)
            order.append(k)

        for k in keys:
            visit(k)
        return order

    # -- linking -----------------------------------------------------------

    def _clever_target_file(self, t) -> Path:
        return self.build_dir / t.target_file.replace("\\", "/")

    def link_target(self, name: str) -> bool:
        t = self.project.targets.get(name)
        if not t:
            print(f"  (no link recipe for target '{name}', skipping link)")
            return True
        out = self._clever_target_file(t)
        out.parent.mkdir(parents=True, exist_ok=True)

        objs = [self._obj_path(u) for u in self.project.units if u.target == name]
        objs = [o for o in objs if o.exists()]

        lib_deps = []
        for lib in t.link_libraries:
            stem = Path(lib.replace("\\", "/")).stem
            if stem in self.project.targets and stem != name:
                lib_deps.append(self._clever_target_file(self.project.targets[stem]))

        need = (
            not out.exists()
            or any(o in self.compiled for o in objs)
            or any(dep.name.replace(".lib", "") in self.rebuilt_targets for dep in lib_deps)
        )
        if not need:
            return True

        if t.kind == "static":
            ok = self._link_static(t, out, objs)
        else:
            ok = self._link_exe(t, out, objs)
        if ok:
            self.rebuilt_targets.add(name)
        return ok

    def _link_static(self, t, out: Path, objs: list[Path]) -> bool:
        print(f"  AR   {t.target_file}")
        if out.exists():
            out.unlink()
        proc = subprocess.run([self.project.ar, "qc", str(out), *map(str, objs)],
                              capture_output=True, text=True)
        if proc.returncode != 0:
            print(f"FAILED ar {t.name}\n{proc.stderr}")
            return False
        subprocess.run([self.project.ranlib, str(out)], capture_output=True, text=True)
        return True

    def _remap_link_lib(self, lib: str) -> str:
        stem = Path(lib.replace("\\", "/")).stem
        if stem in self.project.targets:
            return str(self._clever_target_file(self.project.targets[stem]))
        return lib  # leave relative vcpkg paths; link runs with cwd=cmake build

    def _link_exe(self, t, out: Path, objs: list[Path]) -> bool:
        print(f"  LINK {t.target_file}")
        rsp = self.build_dir / (t.name + ".link.rsp")
        rsp_lines = [str(o) for o in objs]
        for lib in t.link_libraries:
            rsp_lines.append(self._remap_link_lib(lib))
        rsp.write_text("\n".join(rsp_lines), encoding="utf-8")

        cmd = [self.project.compiler, "-nostartfiles", "-nostdlib",
               *t.flags, *t.link_flags, f"@{rsp}", "-o", str(out)]
        cmd += ["-Xlinker", "/MANIFEST:EMBED"]
        if t.implib:
            cmd += ["-Xlinker", f"/implib:{self.build_dir / t.implib.replace(chr(92), '/')}"]
        if t.pdb:
            cmd += ["-Xlinker", f"/pdb:{self.build_dir / t.pdb.replace(chr(92), '/')}"]
        cmd += ["-Xlinker", "/version:0.0"]

        proc = subprocess.run(cmd, capture_output=True, text=True,
                              cwd=str(self.project.cmake_build_dir))
        if proc.stdout.strip():
            print(proc.stdout)
        if proc.returncode != 0:
            print(f"FAILED link {t.name}\n{proc.stderr}")
            return False

        self._run_post_build(t, out)
        return True

    def _run_post_build(self, t, out: Path) -> None:
        if not t.post_build or "applocal" not in t.post_build:
            return
        # Retarget the vcpkg DLL-copy step at clever's exe.
        cmake_out = (self.project.cmake_build_dir / t.target_file.replace("\\", "/"))
        pb = t.post_build.replace(str(cmake_out).replace("\\", "/"), str(out).replace("\\", "/"))
        try:
            subprocess.run(pb, shell=True, capture_output=True, text=True,
                           cwd=str(self.project.cmake_build_dir / Path(t.target_file).parent))
        except OSError:
            pass
