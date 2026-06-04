"""Throwaway prototype: prove libclang can comprehend a C++23 module unit.

Parses one .ixx with the project's real flags and prints, for each exported
declaration: kind, qualified name, USR, mangled name, whether it is
'body-is-interface' (template/inline/constexpr), and the source extents of its
signature vs its body (so we can hash them separately).
"""
import sys, json
from pathlib import Path

import clang.native
import clang.cindex as cx

cx.Config.set_library_file(str(Path(clang.native.__file__).with_name("libclang.dll")))

CCJSON = Path("build/compile_commands.json")
target_file = sys.argv[1] if len(sys.argv) > 1 else \
    "C:/Users/Ryan/Desktop/Ryan/Rev/CAM/source/App/App.ixx"

# Pull the flags clever already harvests.
sys.path.insert(0, str(Path("clever").resolve()))
from _clever import config as cfg
proj = cfg.load(Path("build"))
unit = next(u for u in proj.units if str(u.source).replace("\\", "/").endswith(
    target_file.split("/")[-1]))

args = [a for a in unit.flags if a not in ("-c",)]
# We comprehend with libclang 18 but the project builds with clang 20; the
# MSVC STL hard-errors on clang<19. We are only parsing for AST (never
# codegen), so it is safe to bypass that version guard.
args += ["-x", "c++-module", "-std=c++23",
         "-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH"]

import re
raw = Path(unit.source).read_text(encoding="utf-8", errors="replace")
# Neutralise import lines (keep line count stable) so a missing BMI cannot
# abort the parse. We resolve cross-module references by name, not by BMI.
neutral = "\n".join(
    "" if re.match(r"\s*(export\s+)?import\b", ln) else ln
    for ln in raw.splitlines()
)

index = cx.Index.create()
tu = index.parse(
    str(unit.source), args=args,
    unsaved_files=[(str(unit.source), neutral)],
    options=cx.TranslationUnit.PARSE_INCOMPLETE,
)

print("== diagnostics (first 8) ==")
for d in list(tu.diagnostics)[:8]:
    print(f"  [{d.severity}] {d.spelling}")

BODY_IS_INTERFACE = {
    cx.CursorKind.FUNCTION_TEMPLATE, cx.CursorKind.CLASS_TEMPLATE,
}

def is_inline(c):
    try:
        return c.is_inlined() if hasattr(c, "is_inlined") else False
    except Exception:
        return False

count = 0
tu_file = str(unit.source).replace("\\", "/")
for c in tu.cursor.walk_preorder():
    if c.location.file is None:
        continue
    if str(c.location.file).replace("\\", "/") != tu_file:
        continue
    if c.kind in (cx.CursorKind.FUNCTION_DECL, cx.CursorKind.CXX_METHOD,
                  cx.CursorKind.FUNCTION_TEMPLATE, cx.CursorKind.VAR_DECL,
                  cx.CursorKind.STRUCT_DECL, cx.CursorKind.CLASS_DECL):
        count += 1
        if count > 12:
            break
        ext = c.extent
        try:
            mangled = c.mangled_name
        except Exception:
            mangled = ""
        print(json.dumps({
            "kind": c.kind.name,
            "name": c.spelling,
            "usr": c.get_usr(),
            "mangled": mangled,
            "tmpl": c.kind in BODY_IS_INTERFACE,
            "lines": [ext.start.line, ext.end.line],
        }))
print(f"... ({count} decls seen)")
