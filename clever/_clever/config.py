"""Harvest build configuration from an existing CMake/Ninja `build/` dir.

`clever` does not try to reimplement CMake's package discovery. Instead it
*cooperates* with a one-time CMake configure: CMake's `compile_commands.json`
already records the exact compiler, defines, include paths and language flags
for every source file, and `build.ninja` records the exact link recipe for
every target (including the OpenCASCADE / vcpkg libraries that only CMake knows
how to locate). We read those two artefacts and nothing else.

The result is a `Project` describing:

  * `compiler`        -- path to clang.exe
  * `units`           -- one `Unit` per (target, source-file) compile entry,
                         with the *base* compile flags (module-specific flags
                         stripped, since clever generates its own)
  * `targets`         -- one `Target` per linkable output (static lib / exe)

Re-running CMake configure (e.g. after adding a file) refreshes these inputs;
clever picks the changes up automatically because it keys its harvest cache on
the mtimes of the two files.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from pathlib import Path


# Flags that clever manages itself and must therefore be stripped from the
# harvested per-file command.
_DROP_PREFIX = ("-fmodule-output", "-fmodule-file", "@")
_DROP_WITH_ARG = ("-o", "-c")


@dataclass
class Unit:
    """A single compile step harvested from compile_commands.json."""

    target: str          # owning target name, e.g. "Rev" or "CAMDemo"
    source: Path         # absolute path to the source file
    flags: list[str]     # base compile flags (no -o/-c/module flags)
    is_module: bool      # .ixx module interface unit


@dataclass
class Target:
    """A linkable output harvested from build.ninja."""

    name: str
    kind: str            # "static" | "exe"
    target_file: str     # path relative to the cmake build dir
    flags: list[str] = field(default_factory=list)
    link_flags: list[str] = field(default_factory=list)
    link_libraries: list[str] = field(default_factory=list)
    implib: str | None = None
    pdb: str | None = None
    post_build: str | None = None


@dataclass
class Project:
    cmake_build_dir: Path
    compiler: str
    ar: str
    ranlib: str
    units: list[Unit]
    targets: dict[str, Target]


def _split_command(cmd: str) -> list[str]:
    """Split a ninja/cmake command line into tokens.

    Paths in this project use 8.3 short names or forward slashes and contain no
    spaces, but we still respect double quotes to be safe.
    """
    tokens: list[str] = []
    cur: list[str] = []
    in_q = False
    i = 0
    n = len(cmd)
    while i < n:
        ch = cmd[i]
        # A backslash-escaped quote is a literal quote that stays in the token
        # (e.g. -DPROJECT_ROOT=\"C:/path\" must keep its quotes as part of the
        # macro value).
        if ch == "\\" and i + 1 < n and cmd[i + 1] == '"':
            cur.append('"')
            i += 2
            continue
        if ch == '"':
            in_q = not in_q
            i += 1
            continue
        if ch.isspace() and not in_q:
            if cur:
                tokens.append("".join(cur))
                cur = []
            i += 1
            continue
        cur.append(ch)
        i += 1
    if cur:
        tokens.append("".join(cur))
    return tokens


def _strip_module_flags(tokens: list[str]) -> list[str]:
    out: list[str] = []
    skip_next = False
    for idx, tok in enumerate(tokens):
        if skip_next:
            skip_next = False
            continue
        if tok in _DROP_WITH_ARG:
            skip_next = True
            continue
        if tok == "-x":
            # drop "-x c++-module" (clever re-adds it for module units)
            skip_next = True
            continue
        if tok.startswith(_DROP_PREFIX):
            continue
        out.append(tok)
    return out


def _target_of_output(output: str) -> str | None:
    m = re.search(r"CMakeFiles[\\/]([^\\/]+)\.dir[\\/]", output.replace("\\", "/"))
    return m.group(1) if m else None


def harvest_units(compile_commands: Path) -> tuple[str, list[Unit]]:
    data = json.loads(compile_commands.read_text(encoding="utf-8"))
    compiler = ""
    units: list[Unit] = []
    for entry in data:
        cmd = entry["command"]
        tokens = _split_command(cmd)
        if not compiler:
            compiler = tokens[0]
        target = _target_of_output(entry.get("output", "")) or "unknown"
        src = Path(entry["file"])
        flags = _strip_module_flags(tokens[1:])
        units.append(
            Unit(
                target=target,
                source=src,
                flags=flags,
                is_module=src.suffix == ".ixx",
            )
        )
    return compiler, units


# --- build.ninja link-edge parsing ----------------------------------------

_BUILD_RE = re.compile(r"^build\s+(?P<outs>[^:]+):\s+(?P<rule>\S+)\s")


def _ninja_unescape(s: str) -> str:
    return s.replace("$:", ":").replace("$ ", " ").replace("$$", "$")


def harvest_targets(build_ninja: Path) -> dict[str, Target]:
    lines = build_ninja.read_text(encoding="utf-8", errors="replace").splitlines()
    targets: dict[str, Target] = {}

    i = 0
    while i < len(lines):
        line = lines[i]
        m = _BUILD_RE.match(line)
        if not m or "_LINKER__" not in m.group("rule"):
            i += 1
            continue

        rule = m.group("rule")
        kind = "static" if "STATIC_LIBRARY" in rule else "exe"

        # Collect indented variable assignments for this edge.
        vars: dict[str, str] = {}
        j = i + 1
        while j < len(lines) and lines[j].startswith("  "):
            vm = re.match(r"^  (?P<k>[A-Z_]+) = (?P<v>.*)$", lines[j])
            if vm:
                vars[vm.group("k")] = vm.group("v")
            j += 1

        target_file = _ninja_unescape(vars.get("TARGET_FILE", ""))
        name = Path(target_file.replace("\\", "/")).stem
        if name:
            targets[name] = Target(
                name=name,
                kind=kind,
                target_file=target_file,
                flags=_split_command(vars.get("FLAGS", "")),
                link_flags=_split_command(vars.get("LINK_FLAGS", "")),
                link_libraries=[
                    _ninja_unescape(t) for t in _split_command(vars.get("LINK_LIBRARIES", ""))
                ],
                implib=_ninja_unescape(vars.get("TARGET_IMPLIB", "")) or None,
                pdb=_ninja_unescape(vars.get("TARGET_PDB", "")) or None,
                post_build=vars.get("POST_BUILD") or None,
            )
        i = j

    return targets


def _sibling_tool(compiler: str, name: str) -> str:
    p = Path(compiler.replace("\\", "/"))
    return str(p.with_name(name))


def load(cmake_build_dir: Path) -> Project:
    cc = cmake_build_dir / "compile_commands.json"
    bn = cmake_build_dir / "build.ninja"
    if not cc.exists():
        raise FileNotFoundError(
            f"{cc} not found. Run a CMake configure first "
            f"(cmake -G Ninja -B {cmake_build_dir} ...) so clever can harvest flags."
        )
    if not bn.exists():
        raise FileNotFoundError(f"{bn} not found (needed for link recipes).")

    compiler, units = harvest_units(cc)
    targets = harvest_targets(bn)
    return Project(
        cmake_build_dir=cmake_build_dir,
        compiler=compiler,
        ar=_sibling_tool(compiler, "llvm-ar.exe"),
        ranlib=_sibling_tool(compiler, "llvm-ranlib.exe"),
        units=units,
        targets=targets,
    )
