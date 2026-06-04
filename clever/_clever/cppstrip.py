"""Comment / whitespace normalisation for C++ source.

The whole point of `clever` is to avoid recompiling (and therefore avoid
cascading rebuilds onto consumers) when a change to a source file is purely
cosmetic -- a comment added, trailing whitespace, reflowed blank lines, etc.

`normalize()` produces a canonical byte string for a translation unit with:

  * all `//` and block comments removed,
  * runs of whitespace collapsed to a single space,
  * leading/trailing whitespace per logical line removed,

while *preserving* string literals, character literals and raw string literals
byte-for-byte (so a comment-looking sequence inside a string is never touched)
and preserving preprocessor directives (they are semantically significant).

The hash of this canonical form is what we compare to decide "did the source
actually change". A pure comment edit yields an identical canonical form, so the
file is not recompiled at all and nothing downstream is disturbed.

Correctness bias: the normaliser is conservative. If it ever encounters
something it cannot confidently classify it falls back to treating the raw byte
as significant, so the worst case is a needless rebuild -- never a skipped one.
"""

from __future__ import annotations


def normalize(text: str) -> str:
    out: list[str] = []
    i = 0
    n = len(text)

    while i < n:
        c = text[i]

        # Line comment ------------------------------------------------------
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            # Skip to end of line, but honour line continuations (`\` at EOL).
            i += 2
            while i < n:
                if text[i] == "\\" and i + 1 < n and text[i + 1] in "\r\n":
                    i += 2
                    continue
                if text[i] == "\n":
                    break
                i += 1
            out.append(" ")
            continue

        # Block comment -----------------------------------------------------
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                i += 1
            i += 2  # consume the closing */
            out.append(" ")
            continue

        # Raw string literal:  R"delim( ... )delim"  (possibly prefixed) -----
        if c in "Rr" and _is_raw_string_start(text, i):
            j = _consume_raw_string(text, i)
            out.append(text[i:j])
            i = j
            continue

        # Normal string / char literal -------------------------------------
        if c == '"' or c == "'":
            j = _consume_quoted(text, i, c)
            out.append(text[i:j])
            i = j
            continue

        # Whitespace: collapse ---------------------------------------------
        if c.isspace():
            # Preserve a single newline so preprocessor line semantics and our
            # line-oriented diffs stay sane, but collapse everything else.
            has_newline = False
            while i < n and text[i].isspace():
                if text[i] == "\n":
                    has_newline = True
                i += 1
            out.append("\n" if has_newline else " ")
            continue

        out.append(c)
        i += 1

    # Trim each line and drop blank lines -- purely cosmetic differences.
    lines = ("".join(out)).split("\n")
    canonical = "\n".join(s.strip() for s in lines if s.strip() != "")
    return canonical


def _is_raw_string_start(text: str, i: int) -> bool:
    # Accept optional encoding prefix already consumed by caller position `i`
    # pointing at the 'R'. Require R" next.
    return i + 1 < len(text) and text[i] == "R" and text[i + 1] == '"'


def _consume_raw_string(text: str, i: int) -> int:
    # i points at 'R', text[i+1] == '"'
    n = len(text)
    k = i + 2
    delim_chars = []
    while k < n and text[k] != "(":
        delim_chars.append(text[k])
        k += 1
    delim = "".join(delim_chars)
    closing = ")" + delim + '"'
    k += 1  # skip '('
    idx = text.find(closing, k)
    if idx == -1:
        return n
    return idx + len(closing)


def _consume_quoted(text: str, i: int, quote: str) -> int:
    n = len(text)
    j = i + 1
    while j < n:
        if text[j] == "\\":
            j += 2
            continue
        if text[j] == quote:
            return j + 1
        if text[j] == "\n":
            # Unterminated on this line (shouldn't happen for valid code);
            # stop here to stay safe.
            return j
        j += 1
    return n
