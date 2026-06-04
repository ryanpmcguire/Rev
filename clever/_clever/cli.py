"""Command-line interface for clever."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path

from . import __version__
from . import config as config_mod
from .cache import Cache
from .engine import Engine


def _repo_root(cmake_build: Path) -> Path:
    return cmake_build.parent


def _default_exe_targets(project) -> list[str]:
    return [t.name for t in project.targets.values() if t.kind == "exe"]


def _embed_resources(repo: Path, verbose: bool) -> None:
    script = repo / "Rev" / "scripts" / "Create_Resource_Modules.py"
    if not script.exists():
        return
    print("  GEN  embedded resources")
    cmd = [sys.executable, str(script), "--project-root", str(repo)]
    proc = subprocess.run(cmd, capture_output=True, text=True, cwd=str(repo))
    if verbose and proc.stdout.strip():
        print(proc.stdout)
    if proc.returncode != 0:
        print(f"  (resource embed failed, continuing)\n{proc.stderr}")


def cmd_build(args) -> int:
    cmake_build = Path(args.cmake_build).resolve()
    build_dir = Path(args.build_dir).resolve()
    repo = _repo_root(cmake_build)

    if args.clean and build_dir.exists():
        shutil.rmtree(build_dir)

    if not args.no_embed:
        _embed_resources(repo, args.verbose)

    project = config_mod.load(cmake_build)
    cache = Cache(build_dir / ".clever-cache.json")
    engine = Engine(project, cache, build_dir, jobs=args.jobs, verbose=args.verbose)

    t0 = time.time()
    print("Scanning modules...")
    engine.scan_all()

    targets = args.targets or _default_exe_targets(project)

    if args.dry_run:
        return _dry_run(engine, project, targets)

    print(f"Compiling (targets: {', '.join(targets)}, -j{args.jobs})...")
    ok = engine.compile_targets(targets)
    cache.save()
    if not ok:
        print("Build failed.")
        return 1

    print("Linking...")
    # Link library deps first, then requested targets.
    link_order = []
    for tname in targets:
        t = project.targets.get(tname)
        if t:
            for lib in t.link_libraries:
                stem = Path(lib.replace("\\", "/")).stem
                if stem in project.targets and stem not in link_order:
                    link_order.append(stem)
        if tname not in link_order:
            link_order.append(tname)
    for tname in link_order:
        if not engine.link_target(tname):
            cache.save()
            return 1

    cache.save()
    dt = time.time() - t0
    n = len(engine.compiled)
    print(f"Done in {dt:.1f}s. {n} file(s) compiled, "
          f"{len(engine.rebuilt_targets)} target(s) linked.")
    if n == 0 and not engine.rebuilt_targets:
        print("Everything up to date.")
    return 0


def _dry_run(engine, project, targets) -> int:
    wanted = set(targets)
    for tname in list(targets):
        t = project.targets.get(tname)
        if t:
            for lib in t.link_libraries:
                stem = Path(lib.replace("\\", "/")).stem
                if stem in project.targets:
                    wanted.add(stem)
    dirty = []
    for u in project.units:
        if u.target not in wanted or u.source not in engine.graph.nodes:
            continue
        needs, *_ = engine._needs_compile(u)
        if needs:
            dirty.append(u)
    print(f"\nWould compile {len(dirty)} file(s):")
    for u in sorted(dirty, key=lambda x: (x.target, str(x.source))):
        print(f"  {u.target}/{u.source.name}")
    if not dirty:
        print("  (nothing — up to date)")
    return 0


def cmd_scan(args) -> int:
    cmake_build = Path(args.cmake_build).resolve()
    build_dir = Path(args.build_dir).resolve()
    project = config_mod.load(cmake_build)
    cache = Cache(build_dir / ".clever-cache.json")
    engine = Engine(project, cache, build_dir, verbose=args.verbose)
    g = engine.scan_all()
    cache.save()
    n_mod = sum(1 for n in g.nodes.values() if n.provides)
    print(f"{len(g.nodes)} translation units, {n_mod} module interfaces.")
    print(f"{len(project.targets)} targets: " +
          ", ".join(f"{t.name}({t.kind})" for t in project.targets.values()))
    try:
        order = g.topo_modules()
        print(f"Topological module order computed ({len(order)} modules). No cycles.")
    except RuntimeError as e:
        print(f"WARNING: {e}")
        return 1
    return 0


def cmd_why(args) -> int:
    cmake_build = Path(args.cmake_build).resolve()
    build_dir = Path(args.build_dir).resolve()
    project = config_mod.load(cmake_build)
    cache = Cache(build_dir / ".clever-cache.json")
    engine = Engine(project, cache, build_dir)
    g = engine.scan_all()
    cache.save()
    consumers = g.consumers_of(args.module)
    print(f"Direct importers of '{args.module}' ({len(consumers)}):")
    for c in sorted(consumers, key=str):
        print(f"  {c.name}")
    return 0


def cmd_clean(args) -> int:
    build_dir = Path(args.build_dir).resolve()
    if build_dir.exists():
        shutil.rmtree(build_dir)
        print(f"Removed {build_dir}")
    else:
        print("Nothing to clean.")
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="clever",
        description="Content-aware incremental builder for C++23 modules.",
    )
    p.add_argument("--version", action="version", version=f"clever {__version__}")
    p.add_argument("--cmake-build", default="build",
                   help="CMake/Ninja build dir to harvest flags from (default: build)")
    p.add_argument("--build-dir", default="build-clever",
                   help="clever's own output dir (default: build-clever)")
    p.add_argument("-v", "--verbose", action="store_true")
    sub = p.add_subparsers(dest="cmd", required=True)

    b = sub.add_parser("build", help="incrementally build targets")
    b.add_argument("targets", nargs="*", help="targets to build (default: all executables)")
    b.add_argument("-j", "--jobs", type=int, default=1, help="parallel compile jobs")
    b.add_argument("--dry-run", action="store_true", help="show what would rebuild")
    b.add_argument("--clean", action="store_true", help="wipe build dir first")
    b.add_argument("--no-embed", action="store_true", help="skip resource-embed pre-step")
    b.set_defaults(func=cmd_build)

    s = sub.add_parser("scan", help="scan modules and report the graph")
    s.set_defaults(func=cmd_scan)

    w = sub.add_parser("why", help="show direct importers of a module")
    w.add_argument("module")
    w.set_defaults(func=cmd_why)

    c = sub.add_parser("clean", help="remove clever's build dir")
    c.set_defaults(func=cmd_clean)
    return p


def main(argv=None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (FileNotFoundError, RuntimeError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
