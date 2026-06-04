"""Per-file semantic comprehension via libclang.

libclang's C API hides everything inside an `export module` / `export namespace`
behind a single opaque cursor, so we first produce an *offset-preserving*
de-modularized view of the source: the `module;` / `export module X;` /
`import Y;` lines and every bare `export` keyword are overwritten with spaces of
equal length. Byte offsets, lines and columns are therefore identical to the
original, which lets us:

  * parse the result as ordinary C++ (`-x c++`), exposing every declaration, and
  * map each declaration's source offset back onto the *original* text to decide
    whether it lived under an `export` (i.e. is visible to importers).

For every declaration we record a USR-keyed entry split into a `sig_hash` (the
part importers can see -- the signature) and a `body_hash` (the implementation),
plus a `class` of `abi-only` (body change is private) vs `body-is-interface`
(template / inline / constexpr -- body change propagates to users). We also
record the module name, its imports, and the set of identifier names the file
references (the safe over-approximation used to decide who actually *uses* a
changed symbol).
"""

from __future__ import annotations

import hashlib
import re
from dataclasses import dataclass, field
from pathlib import Path

import clang.native
import clang.cindex as cx

cx.Config.set_library_file(str(Path(clang.native.__file__).with_name("libclang.dll")))

_INDEX = cx.Index.create()

_DECL_KINDS = {
    cx.CursorKind.FUNCTION_DECL,
    cx.CursorKind.CXX_METHOD,
    cx.CursorKind.FUNCTION_TEMPLATE,
    cx.CursorKind.CONSTRUCTOR,
    cx.CursorKind.DESTRUCTOR,
    cx.CursorKind.CONVERSION_FUNCTION,
    cx.CursorKind.VAR_DECL,
    cx.CursorKind.FIELD_DECL,
    cx.CursorKind.STRUCT_DECL,
    cx.CursorKind.CLASS_DECL,
    cx.CursorKind.CLASS_TEMPLATE,
    cx.CursorKind.ENUM_DECL,
    cx.CursorKind.ENUM_CONSTANT_DECL,
    cx.CursorKind.TYPE_ALIAS_DECL,
    cx.CursorKind.TYPEDEF_DECL,
}

_INTERFACE_SCOPES = {
    cx.CursorKind.TRANSLATION_UNIT,
    cx.CursorKind.NAMESPACE,
    cx.CursorKind.STRUCT_DECL,
    cx.CursorKind.CLASS_DECL,
    cx.CursorKind.CLASS_TEMPLATE,
    cx.CursorKind.UNION_DECL,
    cx.CursorKind.ENUM_DECL,
    cx.CursorKind.CLASS_TEMPLATE_PARTIAL_SPECIALIZATION,
}

_BODY_IS_INTERFACE_KINDS = {
    cx.CursorKind.FUNCTION_TEMPLATE,
    cx.CursorKind.CLASS_TEMPLATE,
}

_KEYWORDS_BODY_IS_INTERFACE = re.compile(r"\b(template|inline|constexpr|consteval)\b")


@dataclass
class Symbol:
    usr: str
    name: str
    qual: str
    kind: str
    exported: bool
    sig_hash: str
    body_hash: str
    klass: str  # "abi-only" | "body-is-interface"


@dataclass
class Comprehension:
    source: str
    module: str | None
    imports: list[str]
    provides: dict[str, dict] = field(default_factory=dict)  # usr -> symbol dict
    idents: list[str] = field(default_factory=list)
    parse_errors: int = 0

    def to_json(self) -> dict:
        return {
            "source": self.source,
            "module": self.module,
            "imports": self.imports,
            "provides": self.provides,
            "idents": self.idents,
            "parse_errors": self.parse_errors,
        }


# --- offset-preserving masking & de-modularization -------------------------

def _mask(text: str) -> str:
    """Same-length copy with comment/string/char contents blanked to spaces.

    Keeps braces, semicolons and keywords outside literals intact so brace
    matching and keyword scans are reliable.
    """
    out = list(text)
    i, n = 0, len(text)

    def blank(a: int, b: int) -> None:
        for k in range(a, min(b, n)):
            if out[k] != "\n":
                out[k] = " "

    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = i
            while j < n and text[j] != "\n":
                j += 1
            blank(i, j)
            i = j
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            j = i + 2
            while j + 1 < n and not (text[j] == "*" and text[j + 1] == "/"):
                j += 1
            blank(i, j + 2)
            i = j + 2
            continue
        if c == "R" and i + 1 < n and text[i + 1] == '"':
            k = i + 2
            delim = []
            while k < n and text[k] != "(":
                delim.append(text[k]); k += 1
            close = ")" + "".join(delim) + '"'
            end = text.find(close, k)
            end = n if end == -1 else end + len(close)
            blank(i + 1, end)  # keep the R, blank the literal body
            i = end
            continue
        if c in "\"'":
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2; continue
                if text[j] == c or text[j] == "\n":
                    break
                j += 1
            blank(i + 1, j)
            i = j + 1
            continue
        i += 1
    return "".join(out)


def _blank_comments(text: str) -> str:
    """Offset-preserving copy with only comments blanked (strings preserved).

    Used as the basis for signature/body hashing so that comment and
    whitespace edits never alter a symbol's hash.
    """
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = i
            while j < n and text[j] != "\n":
                out[j] = " "; j += 1
            i = j
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            j = i
            end = i + 2
            while end + 1 < n and not (text[end] == "*" and text[end + 1] == "/"):
                end += 1
            for k in range(j, min(end + 2, n)):
                if out[k] != "\n":
                    out[k] = " "
            i = end + 2
            continue
        if c == "R" and i + 1 < n and text[i + 1] == '"':
            k = i + 2; delim = []
            while k < n and text[k] != "(":
                delim.append(text[k]); k += 1
            close = ")" + "".join(delim) + '"'
            e = text.find(close, k)
            i = n if e == -1 else e + len(close)
            continue
        if c in "\"'":
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2; continue
                if text[j] == c or text[j] == "\n":
                    break
                j += 1
            i = j + 1
            continue
        i += 1
    return "".join(out)


def _match_brace(masked: str, open_pos: int) -> int:
    depth = 0
    for k in range(open_pos, len(masked)):
        if masked[k] == "{":
            depth += 1
        elif masked[k] == "}":
            depth -= 1
            if depth == 0:
                return k
    return len(masked)


def _exported_intervals(masked: str) -> list[tuple[int, int]]:
    """Char intervals (on original offsets) that lie under an `export`."""
    intervals: list[tuple[int, int]] = []
    for m in re.finditer(r"\bexport\b", masked):
        start = m.start()
        j = m.end()
        while j < len(masked) and masked[j].isspace():
            j += 1
        # `export module ...` / `export import ...` are handled elsewhere.
        if masked[j:j + 6] == "module" or masked[j:j + 6] == "import":
            continue
        # `export namespace <name> {`  or  `export {`  -> brace region.
        rest = masked[j:]
        nm = re.match(r"namespace\b[^\{;]*\{", rest)
        if nm:
            brace = j + nm.end() - 1
            intervals.append((start, _match_brace(masked, brace)))
            continue
        if masked[j:j + 1] == "{":
            intervals.append((start, _match_brace(masked, j)))
            continue
        # Single declaration: ends at top-level ';' or a '{...}' body.
        k = j
        depth = 0
        while k < len(masked):
            ch = masked[k]
            if ch == "{":
                intervals.append((start, _match_brace(masked, k)))
                break
            if ch == ";" and depth == 0:
                intervals.append((start, k))
                break
            k += 1
    return intervals


def _demodularize(text: str, masked: str) -> tuple[str, str | None, list[str]]:
    """Return (same-length buffer with module syntax blanked, module, imports)."""
    buf = list(text)
    module: str | None = None
    imports: list[str] = []

    def blank_span(a: int, b: int) -> None:
        for k in range(a, min(b, len(buf))):
            if buf[k] != "\n":
                buf[k] = " "

    # Whole-line module/import statements (detected on masked text).
    for m in re.finditer(r"(?m)^[ \t]*(export[ \t]+)?module[ \t]*;", masked):
        blank_span(m.start(), m.end())
    for m in re.finditer(r"(?m)^[ \t]*export[ \t]+module\b[^;]*;", masked):
        module = text[m.start():m.end()]
        mm = re.search(r"module\s+([A-Za-z0-9_.:]+)", module)
        module = mm.group(1) if mm else module
        blank_span(m.start(), m.end())
    for m in re.finditer(r"(?m)^[ \t]*module\b[^;]*;", masked):
        # plain `module X;` implementation unit, or `module :private;`
        seg = text[m.start():m.end()]
        mm = re.search(r"module\s+([A-Za-z0-9_.:]+)", seg)
        if mm and module is None:
            module = mm.group(1)
        blank_span(m.start(), m.end())
    for m in re.finditer(r"(?m)^[ \t]*(export[ \t]+)?import\b[^;]*;", masked):
        seg = text[m.start():m.end()]
        mm = re.search(r"import\s+([A-Za-z0-9_.:<>\"/ ]+?)\s*;", seg)
        if mm:
            imports.append(mm.group(1).strip())
        blank_span(m.start(), m.end())

    # Remaining bare `export` keywords -> spaces (preserve offsets).
    for m in re.finditer(r"\bexport\b", masked):
        j = m.end()
        while j < len(masked) and masked[j].isspace():
            j += 1
        if masked[j:j + 6] in ("module", "import"):
            continue
        for k in range(m.start(), m.end()):
            buf[k] = " "

    return "".join(buf), module, imports


# --- hashing helpers -------------------------------------------------------

def _h(s: str) -> str:
    return hashlib.sha256(s.encode("utf-8", "replace")).hexdigest()[:16]


def _norm_ws(s: str) -> str:
    return re.sub(r"\s+", " ", s).strip()


def _body_extent(cursor) -> tuple[int, int] | None:
    for ch in cursor.get_children():
        if ch.kind == cx.CursorKind.COMPOUND_STMT:
            e = ch.extent
            return e.start.offset, e.end.offset
    return None


# --- main entry ------------------------------------------------------------

def comprehend(source: Path, flags: list[str], std: str, extra_args: list[str]) -> Comprehension:
    raw = source.read_text(encoding="utf-8", errors="replace")
    masked = _mask(raw)
    buf, module, imports = _demodularize(raw, masked)
    exported = _exported_intervals(masked)

    def is_exported(off: int) -> bool:
        return any(a <= off <= b for a, b in exported)

    # Comment-free, offset-preserving view used purely for hashing, so that a
    # comment or whitespace edit can never change a symbol's sig/body hash.
    hashbuf = _blank_comments(buf)

    args = list(flags) + ["-x", "c++", f"-std={std}", *extra_args]
    tu = _INDEX.parse(
        str(source), args=args,
        unsaved_files=[(str(source), buf)],
        options=cx.TranslationUnit.PARSE_INCOMPLETE,
    )
    errors = sum(1 for d in tu.diagnostics if d.severity >= cx.Diagnostic.Error)

    comp = Comprehension(source=str(source), module=module, imports=imports, parse_errors=errors)
    main = str(source)

    idents: set[str] = set()
    own_names: set[str] = set()

    for c in tu.cursor.walk_preorder():
        f = c.location.file
        if f is None or str(f) != main:
            continue

        if c.kind == cx.CursorKind.IDENTIFIER if hasattr(cx.CursorKind, "IDENTIFIER") else False:
            pass  # not a cursor kind; idents gathered from tokens below

        if c.kind not in _DECL_KINDS or not c.spelling:
            continue
        # Only namespace-scope / record-member / top-level declarations are part
        # of a module's interface. Anything whose semantic parent is a function
        # (local variables, lambda params, block-scope types) is private to a
        # body and must never be treated as a provided symbol.
        sp = c.semantic_parent
        if sp is None or sp.kind not in _INTERFACE_SCOPES:
            continue

        ext = c.extent
        off = ext.start.offset
        try:
            usr = c.get_usr()
        except Exception:
            usr = ""
        if not usr:
            usr = f"{c.kind.name}:{c.displayname}:{off}"
        own_names.add(c.spelling)

        body = _body_extent(c)
        if body:
            sig_text = hashbuf[ext.start.offset:body[0]]
            body_text = hashbuf[body[0]:body[1]]
        else:
            sig_text = hashbuf[ext.start.offset:ext.end.offset]
            body_text = ""

        try:
            type_spelling = c.type.spelling
        except Exception:
            type_spelling = ""

        is_record = c.kind in (cx.CursorKind.STRUCT_DECL, cx.CursorKind.CLASS_DECL,
                               cx.CursorKind.CLASS_TEMPLATE, cx.CursorKind.ENUM_DECL)
        if is_record:
            # A record's *signature* is the shape importers see: its members'
            # declarations (names + types), NOT their bodies or any comments.
            members = []
            for m in c.get_children():
                if m.kind in _DECL_KINDS or m.kind == cx.CursorKind.CXX_BASE_SPECIFIER:
                    try:
                        mt = m.type.spelling
                    except Exception:
                        mt = ""
                    members.append(f"{m.kind.name}:{m.displayname}:{mt}")
            sig_basis = f"{c.kind.name}|{c.displayname}|" + "|".join(members)
            # The record cursor itself carries no body hash; member bodies are
            # tracked under each member's own USR entry.
            body_text = ""
        else:
            sig_basis = f"{c.kind.name}|{c.displayname}|{type_spelling}|{_norm_ws(sig_text)}"

        # Classification: bias toward body-is-interface when unsure.
        klass = "abi-only"
        if c.kind in _BODY_IS_INTERFACE_KINDS:
            klass = "body-is-interface"
        elif _KEYWORDS_BODY_IS_INTERFACE.search(_norm_ws(sig_text)):
            klass = "body-is-interface"
        elif c.kind in (cx.CursorKind.CXX_METHOD, cx.CursorKind.CONSTRUCTOR,
                        cx.CursorKind.DESTRUCTOR, cx.CursorKind.CONVERSION_FUNCTION):
            # in-class definition (body present, lexical parent is a record)
            sp = c.semantic_parent
            if body and sp is not None and sp.kind in (
                cx.CursorKind.STRUCT_DECL, cx.CursorKind.CLASS_DECL,
                cx.CursorKind.CLASS_TEMPLATE,
            ):
                klass = "body-is-interface"

        comp.provides[usr] = {
            "name": c.spelling,
            "qual": c.displayname,
            "kind": c.kind.name,
            "exported": is_exported(off),
            "sig_hash": _h(sig_basis),
            "body_hash": _h(_norm_ws(body_text)),
            "class": klass,
        }

    # Identifier references: every identifier token in the de-modularized buffer.
    for tok in tu.get_tokens(extent=tu.cursor.extent):
        if tok.kind == cx.TokenKind.IDENTIFIER:
            idents.add(tok.spelling)

    comp.idents = sorted(idents - own_names)
    return comp
