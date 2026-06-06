"""Transpiler: lower a C++23 module unit to classic .hpp/.cpp.

Two things happen:

  1. Module syntax is lowered to includes: `module;` / `export module X;` are
     dropped, `import Y;` becomes `#include "<xpp>/Y/<stem>.hpp"`, and the
     leading `export` keyword is stripped.

  2. Stage 2 -- definition relocation. An in-class member function that is
     *defined* in the struct (the ergonomic module style) is split:

         // App.hpp                         // App.cpp
         struct AppState {                  namespace Cam::App {
             bool saveSession();    <--->       bool AppState::saveSession() { ... }
         };                                 }

     so a body edit changes only the .cpp and never the header. Definitions
     that the language requires to stay visible -- templates, members of class
     templates, `constexpr`/`consteval`, deduced (`auto`) return types -- are
     left in place in the header. When in doubt we leave it inline (always
     correct, just less rebuild-optimal).

Output layout (keyed on the unique MODULE NAME):

  .clever/xpp/<Module.Name>/<filestem>.hpp
  .clever/xpp/<Module.Name>/<filestem>.cpp
"""

from __future__ import annotations

import re
from pathlib import Path

import clang.cindex as cx

from .comprehension import _mask, _match_brace, _demodularize  # offset-preserving helpers

_INDEX = cx.Index.create()

_MEMBER_KINDS = {
    cx.CursorKind.CXX_METHOD,
    cx.CursorKind.CONSTRUCTOR,
    cx.CursorKind.DESTRUCTOR,
    cx.CursorKind.CONVERSION_FUNCTION,
}
_FUNC_KINDS = _MEMBER_KINDS | {cx.CursorKind.FUNCTION_DECL}
_NS_SCOPES = {cx.CursorKind.NAMESPACE, cx.CursorKind.TRANSLATION_UNIT}
_RECORD_KINDS = {cx.CursorKind.STRUCT_DECL, cx.CursorKind.CLASS_DECL}
_LEADING_SPECIFIER = re.compile(r"^\s*(?:static|virtual|explicit|friend|inline)\s+")
_STAY_INLINE = re.compile(r"\b(template|constexpr|consteval|auto|decltype)\b")


def hpp_path(xpp_root: Path, module: str, stem: str) -> Path:
    return xpp_root / module / f"{stem}.hpp"


def cpp_path(xpp_root: Path, module: str, stem: str) -> Path:
    return xpp_root / module / f"{stem}.cpp"


def impl_path(xpp_root: Path, rel: str) -> Path:
    return xpp_root / "_impl" / rel


def transpile_impl(rel: str, repo: Path, xpp_root: Path,
                   module_hpp: dict[str, Path]) -> Path:
    """Lower a NON-module source (e.g. main.cpp): rewrite `import` -> `#include`
    and drop module-fragment lines. No header, no relocation -- it defines no
    interface and is included by nobody."""
    src = repo / rel
    raw = src.read_text(encoding="utf-8", errors="replace")
    masked = _mask(raw)
    edits: list[tuple[int, int, str]] = []
    for m in re.finditer(r"(?m)^[ \t]*module[ \t]*;[ \t]*$", masked):
        edits.append((m.start(), m.end(), ""))
    for m in re.finditer(r"(?m)^[ \t]*(?:export[ \t]+)?module\b[^;]*;", masked):
        edits.append((m.start(), m.end(), ""))
    for m in re.finditer(r"(?m)^[ \t]*(?:export[ \t]+)?import[ \t]+([A-Za-z0-9_.:]+)[ \t]*;", masked):
        hpp = module_hpp.get(m.group(1))
        repl = (f'#include "{str(hpp).replace(chr(92), "/")}"' if hpp
                else f"// [clever] unresolved import {m.group(1)}")
        edits.append((m.start(), m.end(), repl))
    out = impl_path(xpp_root, rel)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(
        f"// clever-transpiled implementation source from {rel}\n"
        f'#line 1 "{str(src).replace(chr(92), "/")}"\n{_apply_edits(raw, edits)}\n',
        encoding="utf-8")
    return out


def build_module_hpp_map(digests: dict[str, dict], xpp_root: Path) -> dict[str, Path]:
    out: dict[str, Path] = {}
    for rel, d in digests.items():
        mod = d.get("provides")
        if mod:
            out[mod] = hpp_path(xpp_root, mod, Path(rel).stem)
    return out


def _enclosing(c):
    """Return (namespace_chain, class_chain) of qualified-name parts, or None
    if the method is not a simple namespace>...>class>method (e.g. inside a
    template, which we leave inline)."""
    ns, cls = [], []
    p = c.semantic_parent
    while p is not None and p.kind != cx.CursorKind.TRANSLATION_UNIT:
        if p.kind == cx.CursorKind.NAMESPACE:
            ns.append(p.spelling)
        elif p.kind in _RECORD_KINDS:
            cls.append(p.spelling)
        else:
            return None  # class template, function-local, etc. -> keep inline
        p = p.semantic_parent
    ns.reverse(); cls.reverse()
    return ns, cls


def _method_spans(masked: str, name_off: int):
    """From the name token, locate (params_end, init_list_start|None,
    body_open, body_close). Returns None if there is no `{...}` body (e.g.
    `= default;`) or anything we don't want to touch."""
    n = len(masked)
    i = name_off
    while i < n and masked[i] != "(":
        if masked[i] == ";":
            return None
        i += 1
    if i >= n:
        return None
    depth = 0
    while i < n:
        if masked[i] == "(":
            depth += 1
        elif masked[i] == ")":
            depth -= 1
            if depth == 0:
                break
        i += 1
    params_end = i + 1
    j = params_end
    depth = 0
    init = None
    while j < n:
        ch = masked[j]
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        elif depth == 0 and ch == ";":
            return None  # declaration / =default / =delete
        elif depth == 0 and ch == ":" and masked[j - 1:j] != ":" and masked[j + 1:j + 2] != ":":
            if init is None:
                init = j
        elif depth == 0 and ch == "{":
            return params_end, init, j, _match_brace(masked, j)
        j += 1
    return None


def _var_spans(masked: str, name_off: int):
    """From the variable name token, return (init_start, semicolon). init_start
    is where the initializer begins (`=`/`{`/`(`) or the `;` if none. Returns
    None for multi-declarator statements (`int a, b;`), which we leave alone."""
    n = len(masked)
    i = name_off
    depth = 0
    init = None
    while i < n:
        ch = masked[i]
        if depth == 0 and ch == ",":
            return None
        if depth == 0 and init is None and ch in "={(":
            init = i
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif depth == 0 and ch == ";":
            return (init if init is not None else i), i
        i += 1
    return None


def _relocations(tu, rel: str, raw: str, masked: str):
    """Yield dicts describing each movable namespace/member definition."""
    main = str(rel)
    out = []
    for c in tu.cursor.walk_preorder():
        f = c.location.file
        if f is None or str(f) != main:
            continue
        if not c.is_definition():
            continue

        # --- namespace-scope variables: `T v = ...;` -> extern decl + def ----
        if c.kind == cx.CursorKind.VAR_DECL:
            sp = c.semantic_parent
            if sp is None or sp.kind not in _NS_SCOPES:
                continue  # locals / static members handled elsewhere
            encl = _enclosing(c)
            if encl is None:
                continue
            ns_chain, _ = encl
            decl_start = c.extent.start.offset
            name_off = c.location.offset
            lead = raw[decl_start:name_off]
            # Internal-linkage / can't-extern cases stay in the header.
            try:
                top_const = c.type.is_const_qualified()
            except Exception:
                top_const = True
            if top_const or re.search(r"\b(inline|static|template|auto|decltype)\b", lead):
                continue
            spans = _var_spans(masked, name_off)
            if spans is None:
                continue
            init_start, semi = spans
            declaration = "extern " + raw[decl_start:init_start].rstrip() + ";"
            definition = raw[decl_start:semi + 1]
            out.append({"start": decl_start, "end": semi + 1,
                        "declaration": declaration, "definition": definition,
                        "ns": "::".join(ns_chain)})
            continue

        if c.kind not in _FUNC_KINDS:
            continue
        sp = c.semantic_parent
        is_free = c.kind == cx.CursorKind.FUNCTION_DECL
        if is_free:
            if sp is None or sp.kind not in _NS_SCOPES:
                continue
        else:
            if sp is None or sp.kind not in _RECORD_KINDS:
                continue
        encl = _enclosing(c)
        if encl is None:
            continue
        ns_chain, cls_chain = encl
        decl_start = c.extent.start.offset
        name_off = c.location.offset
        lead = raw[decl_start:name_off]
        # Leave templates / constexpr / deduced-return / tricky operators inline.
        if _STAY_INLINE.search(lead):
            continue
        if c.spelling in ("operator()", "operator[]") or "requires" in lead:
            continue
        # A `static` or `friend` free function has internal / special linkage;
        # leaving it inline in the header is safe and avoids changing meaning.
        if is_free and re.search(r"\b(static|friend)\b", lead):
            continue
        spans = _method_spans(masked, name_off)
        if spans is None:
            continue
        params_end, init, body_open, body_close = spans

        # Declaration kept in the class: signature, no init list, no body.
        decl_end = init if init is not None else body_open
        declaration = raw[decl_start:decl_end].rstrip() + ";"

        # Out-of-line definition for the .cpp.
        lead_clean = _LEADING_SPECIFIER.sub("", lead)
        while _LEADING_SPECIFIER.match(lead_clean):
            lead_clean = _LEADING_SPECIFIER.sub("", lead_clean)
        name_to_params = raw[name_off:params_end]
        post = raw[params_end:body_open]
        post = re.sub(r"\b(override|final)\b", "", post)  # illegal out-of-line
        body = raw[body_open:body_close + 1]
        # Member functions get a `Class::` qualifier; free functions get none.
        qual = ("::".join(cls_chain) + "::") if cls_chain else ""
        definition = f"{lead_clean}{qual}{name_to_params}{post}{body}"

        out.append({
            "start": decl_start, "end": body_close + 1,
            "declaration": declaration,
            "definition": definition,
            "ns": "::".join(ns_chain),
        })
    return out


def _apply_edits(text: str, edits: list[tuple[int, int, str]]) -> str:
    edits = sorted(edits, key=lambda e: e[0])
    out = []
    pos = 0
    for s, e, repl in edits:
        if s < pos:
            continue  # skip overlaps defensively
        out.append(text[pos:s])
        out.append(repl)
        pos = e
    out.append(text[pos:])
    return "".join(out)


def transpile(rel: str, repo: Path, xpp_root: Path,
              module_hpp: dict[str, Path], parse_args: list[str]) -> tuple[Path | None, Path | None]:
    src = repo / rel
    raw = src.read_text(encoding="utf-8", errors="replace")
    masked = _mask(raw)
    buf, module, imports = _demodularize(raw, masked)  # offset-preserving
    if module is None:
        return None, None

    tu = _INDEX.parse(str(rel), args=parse_args,
                      unsaved_files=[(str(rel), buf)],
                      options=cx.TranslationUnit.PARSE_INCOMPLETE)

    edits: list[tuple[int, int, str]] = []

    # 1) module fragment / module decl lines -> removed
    for m in re.finditer(r"(?m)^[ \t]*module[ \t]*;[ \t]*$", masked):
        edits.append((m.start(), m.end(), ""))
    for m in re.finditer(r"(?m)^[ \t]*(?:export[ \t]+)?module\b[^;]*;", masked):
        edits.append((m.start(), m.end(), ""))
    # 2) imports -> includes
    for m in re.finditer(r"(?m)^[ \t]*(?:export[ \t]+)?import[ \t]+([A-Za-z0-9_.:]+)[ \t]*;", masked):
        dep = m.group(1)
        hpp = module_hpp.get(dep)
        repl = (f'#include "{str(hpp).replace(chr(92), "/")}"' if hpp
                else f"// [clever] unresolved import {dep}")
        edits.append((m.start(), m.end(), repl))
    # 3) bare `export` keyword -> removed (namespace/struct/etc.)
    for m in re.finditer(r"\bexport\b[ \t]+", masked):
        # skip those already covered by module/import line edits
        edits.append((m.start(), m.end(), ""))
    # 4) relocate in-class method definitions
    relocs = _relocations(tu, rel, raw, masked)
    defs_by_ns: dict[str, list[str]] = {}
    for r in relocs:
        edits.append((r["start"], r["end"], r["declaration"]))
        defs_by_ns.setdefault(r["ns"], []).append(r["definition"])

    # Build .hpp
    body = _apply_edits(raw, edits)
    stem = Path(rel).stem
    hpp = hpp_path(xpp_root, module, stem)
    cpp = cpp_path(xpp_root, module, stem)
    hpp.parent.mkdir(parents=True, exist_ok=True)
    hpp.write_text(
        f"#pragma once\n// clever-transpiled from {rel}\n"
        f'#line 1 "{str(src).replace(chr(92), "/")}"\n{body}\n',
        encoding="utf-8")

    # Build .cpp
    out = [f"// clever-transpiled implementation unit for module {module}",
           f'#include "{str(hpp).replace(chr(92), "/")}"', ""]
    for ns, defs in defs_by_ns.items():
        if ns:
            out.append(f"namespace {ns} {{")
        out.extend(defs)
        if ns:
            out.append("}")
    cpp.write_text("\n".join(out) + "\n", encoding="utf-8")

    return hpp, cpp
