"""Command-line interface for clever (report-only phase).

Commands:
  init    (re)generate clever.json from the CMake/Ninja build tree
  check   run the dirtiness ladder and report which files clever believes
          must be rebuilt -- WITHOUT building anything
  show    print one file's comprehension (debugging)
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from . import __version__
from . import manifest as manifest_mod


def _load_manifest(args) -> tuple[dict, Path]:
    mpath = Path(args.manifest).resolve()
    if not mpath.exists():
        raise FileNotFoundError(
            f"{mpath} not found. Run `clever init` first to generate it.")
    manifest = manifest_mod.load(mpath)
    repo = mpath.parent
    return manifest, repo


def cmd_init(args) -> int:
    cmake_build = Path(args.cmake_build).resolve()
    out = Path(args.manifest).resolve()
    manifest_mod.write(cmake_build, out)
    m = manifest_mod.load(out)
    nfiles = sum(len(t["sources"]) for t in m["targets"])
    print(f"Wrote {out}")
    print(f"  project: {m['project']}  std: {m['compiler']['std']}")
    print(f"  targets: " + ", ".join(f"{t['name']}({t['kind']}, {len(t['sources'])} files)"
                                     for t in m["targets"]))
    print(f"  {nfiles} source files described.")
    return 0


def cmd_check(args) -> int:
    from . import ladder  # imports libclang; defer so `init` works without it
    from .store import Store

    manifest, repo = _load_manifest(args)
    store = Store(repo / args.build_dir)

    # `check` is a read-only query: it never advances the baseline.
    report = ladder.run(manifest, repo, store, update=False)

    if args.json:
        print(json.dumps({
            "baseline": report.baseline,
            "changed": report.changed,
            "rebuild": report.rebuild,
            "verdicts": [vars(v) for v in report.verdicts],
        }, indent=2))
        return 0

    if report.baseline:
        print(f"Baseline established: staged {len(report.changed)} files into "
              f"{(repo / args.build_dir)}.")
        print("Artifacts per file: <name>.digest.json / .ast.json / .graph.json")
        print("Edit a file and run `clever check` again to see impact analysis.")
        return 0

    # Build-itinerary buckets.
    touched = [v for v in report.verdicts if v.level in ("L1", "L2")]
    changed = [v for v in report.verdicts if v.self_dirty]
    changed_rels = {v.rel for v in changed}

    print("clever build itinerary")
    print("=" * 60)

    print(f"\nTouched ({len(touched)})  -- timestamp moved, no real change:")
    if not touched:
        print("  (none)")
    for v in sorted(touched, key=lambda x: x.rel):
        why = "identical bytes" if v.level == "L1" else "comments/whitespace only"
        print(f"  {v.rel}")
        print(f"      {why}")

    print(f"\nChanged ({len(changed)})  -- content actually changed:")
    if not changed:
        print("  (none)")
    for v in sorted(changed, key=lambda x: x.rel):
        print(f"  {v.rel}")
        print(f"      {v.summary}")

    # Consumers pulled in purely by impact (not themselves edited).
    impacted = {rel: why for rel, why in report.rebuild.items() if rel not in changed_rels}
    print(f"\nNeed rebuilding ({len(report.rebuild)}):")
    if not report.rebuild:
        print("  (nothing -- every change was cosmetic)")
    for v in sorted(changed, key=lambda x: x.rel):
        print(f"  {v.rel}")
        print(f"      changed source")
    for rel, why in sorted(impacted.items()):
        print(f"  {rel}")
        print(f"      consumer: {why}")
    return 0


def cmd_build(args) -> int:
    from . import ladder
    from .store import Store
    from .builder import Builder

    manifest, repo = _load_manifest(args)
    store = Store(repo / args.build_dir)

    if not args.no_embed:
        _embed_resources(repo, args.verbose)

    print("Analysing dirtiness...")
    # Read-only analysis -- the baseline is advanced ONLY after a successful
    # build, and only for files actually compiled (see commit_baseline).
    report = ladder.run(manifest, repo, store, update=False)
    rebuild_set = set(report.rebuild.keys())
    if args.force:
        rebuild_set = set(report.digests.keys())
        print("(--force: recompiling everything)")
    elif report.baseline:
        print("(first run -- no prior baseline, so a full build will follow)")
    else:
        print(f"Ladder says {len(rebuild_set)} file(s) need rebuilding"
              f"{' (plus any missing objects)' if rebuild_set else ''}.")

    builder = Builder(manifest, repo, store, rebuild_set,
                      digests=report.digests, verbose=args.verbose)

    if args.check:
        plan = builder.plan()
        comp = plan["compile"]
        print(f"\nWould compile {len(comp)} translation unit(s) "
              f"(in dependency order):")
        if not comp:
            print("  (nothing -- all objects present and up to date)")
        for rel, missing in comp:
            tag = "missing object" if missing else "dirty"
            print(f"  {rel}   [{tag}]")
        print(f"\nWould link: {', '.join(plan['link']) or 'nothing'}")
        return 0

    ok = builder.build()

    # An incremental compile failure is most often a stale/inconsistent BMI set
    # (a module was rebuilt but its importers' BMIs still reference the old one,
    # which clang rejects). The guaranteed-correct recovery is a full rebuild,
    # so offer one rather than leaving the tree half-built.
    if not ok and not args.force:
        if _prompt_force():
            print("Forcing full rebuild for a consistent BMI set...")
            builder = Builder(manifest, repo, store, set(report.digests.keys()),
                              digests=report.digests, verbose=args.verbose)
            ok = builder.build()

    if not ok:
        print("Build failed.")
        return 1

    # Now that the compile succeeded, advance the baseline for exactly the files
    # we rebuilt -- so a subsequent `check` correctly sees them as clean and an
    # interrupted/failed build never marks unbuilt files as up to date.
    ladder.commit_baseline(store, report, builder.compiled)

    print(f"Build OK. {len(builder.compiled)} compiled, "
          f"linked: {', '.join(sorted(builder.__dict__.get('_relinked', set()))) or 'nothing'}.")

    if args.run:
        return builder.run(args.target)
    return 0


def _prompt_force() -> bool:
    print("\nBuild failed -- the incremental BMI set looks too stale/inconsistent")
    print("(a module was rebuilt but its importers were not, so clang rejects the mix).")
    if not sys.stdin.isatty():
        print("Re-run with --force to rebuild everything.")
        return False
    try:
        ans = input("Too stale -- force a full rebuild? [y/N] ").strip().lower()
    except (EOFError, KeyboardInterrupt):
        return False
    return ans in ("y", "yes")


def _embed_resources(repo: Path, verbose: bool) -> None:
    import subprocess
    script = repo / "Rev" / "scripts" / "Create_Resource_Modules.py"
    if not script.exists():
        return
    print("  GEN  embedded resources")
    p = subprocess.run([sys.executable, str(script), "--project-root", str(repo)],
                       capture_output=True, text=True, cwd=str(repo))
    if p.returncode != 0:
        print(f"  (resource embed failed, continuing)\n{p.stderr}")


def cmd_show(args) -> int:
    from . import comprehension as comp_mod
    from . import ladder

    manifest, repo = _load_manifest(args)
    rel = args.file.replace("\\", "/")
    tindex = ladder._target_index(manifest)
    target = tindex.get(rel)
    if not target:
        # try to match by suffix
        for s, t in tindex.items():
            if s.endswith(rel):
                rel, target = s, t
                break
    if not target:
        print(f"error: {rel} not found in any target", file=sys.stderr)
        return 2
    flags = ladder._parse_flags(manifest, repo, target)
    c = comp_mod.comprehend(repo / rel, flags, manifest["compiler"]["std"],
                            manifest.get("comprehend", {}).get("extra_args", []))
    print(json.dumps(c.to_json(), indent=2))
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="clever",
        description="Content-aware dirtiness analyser for C++23 modules (report-only).",
    )
    p.add_argument("--version", action="version", version=f"clever {__version__}")
    p.add_argument("--manifest", default="clever.json", help="project manifest (default: clever.json)")
    p.add_argument("--build-dir", default=".clever", help="clever artifact dir (default: .clever)")
    sub = p.add_subparsers(dest="cmd", required=True)

    i = sub.add_parser("init", help="generate clever.json from the CMake build tree")
    i.add_argument("--cmake-build", default="build", help="CMake/Ninja build dir (default: build)")
    i.set_defaults(func=cmd_init)

    c = sub.add_parser("check", help="report which files need rebuilding (read-only, no build)")
    c.add_argument("--json", action="store_true", help="machine-readable output")
    c.set_defaults(func=cmd_check)

    b = sub.add_parser("build", help="compile the dirty set, link, and optionally run")
    b.add_argument("--check", action="store_true",
                   help="dry run: print the compile/link plan without building")
    b.add_argument("--force", action="store_true",
                   help="recompile every translation unit (ignore the baseline)")
    b.add_argument("--run", action="store_true", help="run the executable after a successful build")
    b.add_argument("--target", help="which executable to run (default: first exe)")
    b.add_argument("--no-embed", action="store_true", help="skip the resource-embed pre-step")
    b.add_argument("-v", "--verbose", action="store_true")
    b.set_defaults(func=cmd_build)

    s = sub.add_parser("show", help="print one file's comprehension (debug)")
    s.add_argument("file")
    s.set_defaults(func=cmd_show)
    return p


def main(argv=None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (FileNotFoundError, RuntimeError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
