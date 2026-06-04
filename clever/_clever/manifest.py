"""The clever project manifest (`clever.json`).

This is clever's analogue of a CMakeLists: a single JSON file that fully
describes how to comprehend, compile and link the project -- compiler, language
standard, include directories, preprocessor defines, per-target source sets,
and link recipes. Once generated, clever reads *this* and nothing else; a CMake
build tree is only needed to (re)generate it when dependencies change.

`generate()` harvests an initial manifest from an existing CMake/Ninja build
tree (compile_commands.json for per-target compile flags, build.ninja for link
recipes). The result is meant to be committed and hand-editable.
"""

from __future__ import annotations

import json
from pathlib import Path

from . import config as config_mod


def _classify_flags(flags: list[str]) -> tuple[list[str], list[str], list[str], str | None]:
    """Split a compile flag list into (includes, defines, other, std)."""
    includes: list[str] = []
    defines: list[str] = []
    other: list[str] = []
    std: str | None = None
    i = 0
    while i < len(flags):
        f = flags[i]
        if f.startswith("-I"):
            includes.append(f[2:] if len(f) > 2 else flags[(i := i + 1)])
        elif f.startswith("-D"):
            defines.append(f[2:] if len(f) > 2 else flags[(i := i + 1)])
        elif f.startswith("-std="):
            std = f[5:]
        elif f == "-x":
            i += 1  # drop "-x c++-module"; clever re-adds per unit
        else:
            other.append(f)
        i += 1
    return includes, defines, other, std


def _rel(p: str, repo: Path) -> str:
    try:
        return str(Path(p.replace("\\", "/")).resolve().relative_to(repo.resolve())).replace("\\", "/")
    except (ValueError, OSError):
        return p.replace("\\", "/")


def generate(cmake_build: Path) -> dict:
    repo = cmake_build.parent
    project = config_mod.load(cmake_build)

    # Representative compile flags per target (every file in a CMake target
    # shares the same base flags; only module wiring differs per file).
    per_target_units: dict[str, list] = {}
    for u in project.units:
        per_target_units.setdefault(u.target, []).append(u)

    std_global: str | None = None
    compiler = project.compiler

    targets_out = []
    for tname, units in per_target_units.items():
        rep = next((u for u in units if u.is_module), units[0])
        includes, defines, other, std = _classify_flags(rep.flags)
        std_global = std_global or std

        tgt = project.targets.get(tname)
        sources = sorted({_rel(str(u.source), repo) for u in units})

        entry: dict = {
            "name": tname,
            "kind": tgt.kind if tgt else ("static" if tname.endswith("Rev") else "exe"),
            "include_dirs": [_rel(p, repo) for p in includes],
            "defines": defines,
            "compile_flags": other,
            "sources": sources,
        }
        if tgt:
            entry["output"] = tgt.target_file.replace("\\", "/")
            depends = []
            link_libs = []
            for lib in tgt.link_libraries:
                stem = Path(lib.replace("\\", "/")).stem
                if stem in project.targets and stem != tname:
                    depends.append(stem)
                else:
                    link_libs.append(lib.replace("\\", "/"))
            if depends:
                entry["depends"] = depends
            if tgt.kind == "exe":
                entry["link_libraries"] = link_libs
                entry["link_flags"] = tgt.link_flags
                if tgt.implib:
                    entry["implib"] = tgt.implib.replace("\\", "/")
                if tgt.pdb:
                    entry["pdb"] = tgt.pdb.replace("\\", "/")
                if tgt.post_build and "applocal" in (tgt.post_build or ""):
                    entry["post_build"] = tgt.post_build
        targets_out.append(entry)

    manifest = {
        "project": "RevRoot",
        "compiler": {
            "cxx": compiler.replace("\\", "/"),
            "ar": project.ar.replace("\\", "/"),
            "ranlib": project.ranlib.replace("\\", "/"),
            "std": std_global or "c++23",
        },
        "comprehend": {
            # Extra args used only when libclang parses for AST comprehension.
            # libclang (18) is older than the build compiler (20); the MSVC STL
            # version guard must be bypassed since we only parse, never codegen.
            "extra_args": ["-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH"],
        },
        "cmake_build_dir": _rel(str(cmake_build), repo),
        "targets": targets_out,
    }
    return manifest


def write(cmake_build: Path, out_path: Path) -> Path:
    manifest = generate(cmake_build)
    out_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return out_path


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))
