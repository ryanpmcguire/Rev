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
    # Flatten drive/leading separators so an absolute `rel` can't escape the
    # output dir and land next to the source.
    import os
    _, tail = os.path.splitdrive(rel.replace("\\", "/"))
    return xpp_root / "_impl" / tail.lstrip("/")


def transpile_impl(rel: str, repo: Path, xpp_root: Path,
                   module_hpp: dict[str, Path],
                   project_root: Path | None = None) -> tuple[Path, list[str]]:
    """Lower a NON-module source (e.g. main.cpp): rewrite `import` -> `#include`
    and drop module-fragment lines. No header, no relocation -- it defines no
    interface and is included by nobody."""
    src = repo / rel
    res_paths: list[str] = []
    raw = src.read_bytes().decode("latin-1")
    masked = _mask(raw)
    edits: list[tuple[int, int, str]] = _include_edits(raw, masked, src.parent)
    for m in re.finditer(r"(?m)^[ \t]*module[ \t]*;[ \t]*\r?$", masked):
        edits.append((m.start(), m.end(), ""))
    for m in re.finditer(r"(?m)^[ \t]*(?:export[ \t]+)?module\b[^;]*;", masked):
        edits.append((m.start(), m.end(), ""))
    for m in re.finditer(r"(?m)^[ \t]*(?:export[ \t]+)?import[ \t]+([A-Za-z0-9_.:]+)[ \t]*;", masked):
        hpp = module_hpp.get(m.group(1))
        repl = (f'#include "{str(hpp).replace(chr(92), "/")}"' if hpp
                else f"// [clever] unresolved import {m.group(1)}")
        edits.append((m.start(), m.end(), repl))
    has_managed = bool(_MANAGED_RE.search(masked))
    if has_managed:
        for m in _MANAGED_INC_RE.finditer(masked):
            edits.append((m.start(), m.end(), ""))
    out = impl_path(xpp_root, rel)
    out.parent.mkdir(parents=True, exist_ok=True)
    body = _apply_edits(raw, edits)
    if has_managed:
        body, res_paths = _expand_file_macro(body, src.parent, project_root)
    text = (f"// clever-transpiled implementation source from {rel}\n"
            f'#line 1 "{str(src).replace(chr(92), "/")}"\n{body}\n')
    text = _inject_unleak(text)
    out.write_bytes(text.encode("latin-1"))
    return out, sorted(set(res_paths)), []


def build_module_hpp_map(digests: dict[str, dict], xpp_root: Path) -> dict[str, Path]:
    out: dict[str, Path] = {}
    for rel, d in digests.items():
        mod = d.get("provides")
        if mod:
            out[mod] = hpp_path(xpp_root, mod, Path(rel).stem)
    return out


_SYMBOL_KINDS = {
    cx.CursorKind.STRUCT_DECL, cx.CursorKind.CLASS_DECL, cx.CursorKind.ENUM_DECL,
    cx.CursorKind.CLASS_TEMPLATE, cx.CursorKind.FUNCTION_DECL,
    cx.CursorKind.TYPE_ALIAS_DECL, cx.CursorKind.TYPEDEF_DECL,
}


def _ns_qual(c) -> str:
    """Fully-qualified name of a NAMESPACE cursor, e.g. 'Rev::Element'."""
    parts = []
    while c is not None and c.kind == cx.CursorKind.NAMESPACE:
        if c.spelling:
            parts.append(c.spelling)
        c = c.semantic_parent
    return "::".join(reversed(parts))


def _collect_symbols(tu, rel: str) -> list[list[str]]:
    """Top-level [namespace, name] pairs this file exports -- used to build the
    cross-file collision map (names defined in >1 namespace)."""
    out, seen = [], set()
    main = str(rel)
    for c in tu.cursor.walk_preorder():
        f = c.location.file
        if f is None or str(f) != main or c.kind not in _SYMBOL_KINDS or not c.spelling:
            continue
        sp = c.semantic_parent
        if sp is not None and sp.kind == cx.CursorKind.NAMESPACE:
            key = (_ns_qual(sp), c.spelling)
            if key not in seen:
                seen.add(key)
                out.append([key[0], key[1]])
    return out


def qualify_text(text: str, name_to_target: dict[str, str]) -> str:
    """Rewrite each *unqualified* use of a collision name to `Target::name`.
    Skips members/qualified uses (`x.name`, `a::name`, `p->name`) and the
    name's own definition (`struct name`, `namespace name`)."""
    if not name_to_target:
        return text
    masked = _mask(text)
    edits = []
    for name, target in name_to_target.items():
        pat = re.compile(r"\b" + re.escape(name) + r"\b")
        for m in pat.finditer(masked):
            s = m.start()
            j = s - 1
            while j >= 0 and text[j] in " \t":
                j -= 1
            if j >= 0 and text[j] in ":.>":          # ::name / .name / ->name
                continue
            k = j
            while k >= 0 and (text[k].isalnum() or text[k] == "_"):
                k -= 1
            if text[k + 1:j + 1] in ("struct", "class", "enum", "union", "namespace"):
                continue
            edits.append((s, m.end(), f"{target}::{name}"))
    return _apply_edits(text, edits)


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


def _include_edits(raw: str, masked: str, src_dir: Path) -> list[tuple[int, int, str]]:
    """Rewrite file-relative quoted includes (`#include "../x.hpp"`) to absolute
    paths, since the transpiled file no longer sits next to the original. Quoted
    includes that resolve via -I (not relative to the source) are left as-is."""
    edits = []
    for m in re.finditer(r'(?m)^[ \t]*#[ \t]*include[ \t]+"([^"\n]+)"', raw):
        if "include" not in masked[m.start():m.end()]:
            continue  # the match sits inside a comment
        path = m.group(1)
        if Path(path).is_absolute():
            continue
        cand = src_dir / path
        try:
            ok = cand.exists()
        except OSError:
            ok = False
        if ok:
            edits.append((m.start(1), m.end(1), str(cand.resolve()).replace("\\", "/")))
    return edits


_MANAGED_RE = re.compile(r'[<"]managed\.hpp[>"]')
# `\r?$` so CRLF files match (a trailing \r is not [ \t]).
_MANAGED_INC_RE = re.compile(r'(?m)^[ \t]*#[ \t]*include[ \t]*[<"]managed\.hpp[>"][ \t]*\r?$')


_STR_LIT_RE = re.compile(r'^\s*"((?:[^"\\]|\\.)*)"\s*$')


def _resolve_resource(path: str, src_dir: Path, project_root: Path | None) -> Path | None:
    """Resolve a File("...") path the way Resource::FromFile does: a `./` prefix
    is relative to the source file's directory; otherwise it's project-root
    relative."""
    clean = path.replace("\\", "/")
    if clean.startswith("./"):
        cand = (src_dir / clean[2:])
    elif project_root is not None:
        cand = (project_root / clean)
    else:
        return None
    try:
        cand = cand.resolve()
        return cand if cand.is_file() else None
    except OSError:
        return None


def _embed_literal(data: str) -> str:
    """A C++ string literal holding `data` (latin-1 chars == raw bytes).

    Text resources (valid UTF-8) use a raw string literal (compact, fast to
    compile). Binary resources (e.g. fonts) would be invalid UTF-8 inside the
    .cpp, so they are emitted as octal escapes in an ordinary literal -- always
    valid source, unambiguous (3-digit octal), handles any byte."""
    raw_bytes = data.encode("latin-1")
    try:
        raw_bytes.decode("utf-8")
        is_text = True
    except UnicodeDecodeError:
        is_text = False
    if is_text:
        delim = "CLEVERRES"
        while (")" + delim + '"') in data:
            delim += "X"
        return f'R"{delim}({data}){delim}"'
    return '"' + "".join(f"\\{b:03o}" for b in raw_bytes) + '"'


def _expand_file_macro(text: str, src_dir: Path | None = None,
                       project_root: Path | None = None) -> tuple[str, list[str]]:
    """Replay managed.hpp's `File(path)` macro. A `File("literal")` whose path
    resolves to a real file is EMBEDDED as `Resource::FromString(<bytes>, <n>)`
    (so resources are baked in and re-embedded when they change); anything else
    falls back to `Resource::FromFile(__FILE__, (args))`. Returns the rewritten
    text and the list of embedded resource file paths (for dirtiness tracking)."""
    masked = _mask(text)
    edits = []
    res_paths: list[str] = []
    for m in re.finditer(r"\bFile\b", masked):
        j = m.end()
        while j < len(masked) and masked[j] in " \t\r\n":
            j += 1
        if j >= len(masked) or masked[j] != "(":
            continue
        depth = 0
        k = j
        while k < len(masked):
            if masked[k] == "(":
                depth += 1
            elif masked[k] == ")":
                depth -= 1
                if depth == 0:
                    break
            k += 1
        if k >= len(masked):
            continue
        args = text[j + 1:k]
        repl = None
        lit = _STR_LIT_RE.match(args)
        if lit and src_dir is not None:
            res = _resolve_resource(lit.group(1), src_dir, project_root)
            if res is not None:
                data = res.read_bytes().decode("latin-1")
                repl = f"::Rev::Core::Resource::FromString({_embed_literal(data)}, {len(data)})"
                res_paths.append(str(res).replace("\\", "/"))
        if repl is None:
            repl = f"::Rev::Core::Resource::FromFile(__FILE__, ({args}))"
        edits.append((m.start(), k + 1, repl))
    return _apply_edits(text, edits), res_paths


_UNLEAK = ["interface"]  # windows.h macro that clobbers identifiers; NOT far/near
                         # (those are structural -- windows.h's own FD_ZERO uses FAR)


def _inject_unleak(text: str) -> str:
    """After the include block, #undef the windows.h macros that clobber common
    identifiers (modules isolate these; headers leak them). Placed after the
    last #include so it runs once windows.h has defined them."""
    lines = text.split("\n")
    last_inc = -1
    for idx, ln in enumerate(lines):
        if ln.lstrip().startswith("#include"):
            last_inc = idx
    if last_inc < 0:
        return text
    block = []
    for macro in _UNLEAK:
        block += [f"#ifdef {macro}", f"#undef {macro}", "#endif"]
    block.append("// [clever] un-leaked windows.h identifier macros")
    lines[last_inc + 1:last_inc + 1] = block
    return "\n".join(lines)


def _strip_defaults(s: str) -> str:
    """Remove `= <default>` from a parameter list `name(params)`. Defaults are
    legal only on the declaration, never the out-of-line definition."""
    i = s.find("(")
    if i < 0:
        return s
    res = list(s[:i + 1])
    depth = 1
    skipping = False
    k = i + 1
    while k < len(s):
        ch = s[k]
        if (depth == 1 and not skipping and ch == "="
                and s[k - 1] not in "=<>!" and (k + 1 >= len(s) or s[k + 1] != "=")):
            skipping = True
            k += 1
            continue
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
            if depth == 0:
                res.append(ch)
                res.append(s[k + 1:])
                return "".join(res)
        if depth == 1 and skipping and ch == ",":
            skipping = False
        if not skipping:
            res.append(ch)
        k += 1
    return "".join(res)


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

        # A member DEFINED out-of-line (`void Arc2::foo(){...}` at namespace
        # scope -- lexical parent is a namespace, not the record) is already a
        # qualified definition AND the class already declares it. Move the whole
        # thing to the .cpp and leave nothing in the header (emitting a qualified
        # *declaration* there would be illegal: "out-of-line declaration of a
        # member must be a definition").
        if (not is_free and c.lexical_parent is not None
                and c.lexical_parent.kind in _NS_SCOPES):
            out.append({
                "start": decl_start, "end": body_close + 1,
                "declaration": "",
                "definition": raw[decl_start:body_close + 1],
                "ns": "::".join(ns_chain),
            })
            continue

        # Declaration kept in the class: signature, no init list, no body.
        # Strip the `inline` keyword: we are externalising the definition into
        # the .cpp, so the function must NOT stay inline (an inline function
        # whose body lives only in one .cpp emits no usable external symbol ->
        # undefined at link). `static`/`virtual`/`explicit` are preserved.
        decl_end = init if init is not None else body_open
        declaration = re.sub(r"\binline\b\s*", "", raw[decl_start:decl_end]).rstrip() + ";"

        # Out-of-line definition for the .cpp.
        lead_clean = _LEADING_SPECIFIER.sub("", lead)
        while _LEADING_SPECIFIER.match(lead_clean):
            lead_clean = _LEADING_SPECIFIER.sub("", lead_clean)
        ret = lead_clean.strip()
        name_to_params = _strip_defaults(raw[name_off:params_end])  # defaults only on decl
        post = re.sub(r"\b(override|final)\b", "", raw[params_end:body_open]).strip()
        sep = (" " + post) if post else ""
        body = raw[body_open:body_close + 1]
        qual = ("::".join(cls_chain) + "::") if cls_chain else ""
        # For member functions with a return type, use a trailing return type so
        # a nested return type (e.g. `State` == `Animator::State`) resolves in
        # the class scope. Constructors/destructors/conversions have no return
        # type; free functions resolve their return type at namespace scope.
        if ret and cls_chain and c.kind == cx.CursorKind.CXX_METHOD:
            definition = f"auto {qual}{name_to_params}{sep} -> {ret} {body}"
        else:
            head = (ret + " ") if ret else ""
            definition = f"{head}{qual}{name_to_params}{sep} {body}"

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


def _project_root_from_args(parse_args: list[str]) -> Path | None:
    for a in parse_args:
        if a.startswith("-DPROJECT_ROOT="):
            return Path(a.split("=", 1)[1].strip('"').strip())
    return None


def transpile(rel: str, repo: Path, xpp_root: Path, module_hpp: dict[str, Path],
              parse_args: list[str]) -> tuple[Path | None, Path | None, list[str]]:
    src = repo / rel
    project_root = _project_root_from_args(parse_args)
    # libclang reports offsets in UTF-8 *bytes*, but we index Python strings by
    # character. Round-tripping the UTF-8 bytes through latin-1 yields a string
    # where 1 char == 1 byte, so string indices and libclang byte offsets match
    # exactly even when the source contains non-ASCII (e.g. in comments).
    utf8 = src.read_bytes()
    raw = utf8.decode("latin-1")
    masked = _mask(raw)
    buf, module, imports = _demodularize(raw, masked)  # offset-preserving
    if module is None:
        return None, None, [], []
    res_paths: list[str] = []

    # Hand libclang the real UTF-8 bytes (latin-1 re-encode reconstructs them).
    # A fresh Index per call keeps transpilation thread-safe (a single shared
    # libclang Index is not safe for concurrent parses).
    tu = cx.Index.create().parse(str(rel), args=parse_args,
                                 unsaved_files=[(str(rel), buf.encode("latin-1"))],
                                 options=cx.TranslationUnit.PARSE_INCOMPLETE)

    edits: list[tuple[int, int, str]] = []

    # 1) module fragment / module decl lines -> removed
    for m in re.finditer(r"(?m)^[ \t]*module[ \t]*;[ \t]*\r?$", masked):
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
    # 3b) rewrite file-relative quoted includes to absolute paths
    edits += _include_edits(raw, masked, src.parent)
    # 3c) managed.hpp leaks a `File(path)` macro that clobbers any `struct File`
    #     and constructor calls. Drop the include here; the macro is replayed
    #     textually below (only for files that actually included it).
    has_managed = bool(_MANAGED_RE.search(masked))
    if has_managed:
        for m in _MANAGED_INC_RE.finditer(masked):
            edits.append((m.start(), m.end(), ""))
    # 4) relocate in-class method definitions
    relocs = _relocations(tu, rel, raw, masked)
    defs_by_ns: dict[str, list[str]] = {}
    for r in relocs:
        edits.append((r["start"], r["end"], r["declaration"]))
        defs_by_ns.setdefault(r["ns"], []).append(r["definition"])

    # Single-header-library IMPLEMENTATION blocks (`#define X_IMPLEMENTATION` +
    # the includes right after) must be compiled in exactly ONE TU. As a header
    # they would leak into every includer -> duplicate symbols. Move the whole
    # block to the top of the .cpp (so the bodies compile once there) and strip
    # the *defines* from the .hpp (its include then brings declarations only).
    impl_block = ""
    impl_defs = list(re.finditer(
        r"(?m)^[ \t]*#[ \t]*define[ \t]+\w+_IMPLEMENTATION\b[^\n]*\r?$", masked))
    if impl_defs:
        bs = impl_defs[0].start()
        pos = bs
        while pos < len(masked):
            le = masked.find("\n", pos)
            le = len(masked) if le == -1 else le + 1
            st = masked[pos:le].strip()
            if st.startswith("#define") or st.startswith("#include"):
                pos = le
            else:
                break
        impl_block = raw[bs:pos].rstrip()
        for m in impl_defs:
            edits.append((m.start(), m.end(), ""))   # strip defines from header

    # Build .hpp
    body = _apply_edits(raw, edits)
    if has_managed:
        body, rp = _expand_file_macro(body, src.parent, project_root)  # inline-body File(...)
        res_paths += rp
    stem = Path(rel).stem
    hpp = hpp_path(xpp_root, module, stem)
    cpp = cpp_path(xpp_root, module, stem)
    hpp.parent.mkdir(parents=True, exist_ok=True)
    # Re-encode latin-1 -> bytes restores the original UTF-8 byte stream.
    # NOTE: do NOT un-leak `interface` in HEADERS. A header's `#undef interface`
    # would strip the macro before a *later*-included Windows header (e.g.
    # shobjidl.h's `typedef interface IFoo`) needs it. The un-leak is applied
    # only in the .cpp (the final TU, after all system headers are included).
    hpp_text = (f"#pragma once\n// clever-transpiled from {rel}\n"
                f'#line 1 "{str(src).replace(chr(92), "/")}"\n{body}\n')
    hpp.write_bytes(hpp_text.encode("latin-1"))

    # Build .cpp. The IMPLEMENTATION block (if any) must come BEFORE the hpp
    # include so its `#define`d impl include is the first one in the TU.
    out = [f"// clever-transpiled implementation unit for module {module}"]
    if impl_block:
        out.append(impl_block)
    out += [f'#include "{str(hpp).replace(chr(92), "/")}"', ""]
    for ns, defs in defs_by_ns.items():
        if ns:
            out.append(f"namespace {ns} {{")
        out.extend(defs)
        if ns:
            out.append("}")
    cpp_text = "\n".join(out) + "\n"
    if has_managed:
        cpp_text, rp = _expand_file_macro(cpp_text, src.parent, project_root)  # relocated-body File(...)
        res_paths += rp
    cpp_text = _inject_unleak(cpp_text)
    cpp.write_bytes(cpp_text.encode("latin-1"))

    return hpp, cpp, sorted(set(res_paths)), _collect_symbols(tu, rel)
