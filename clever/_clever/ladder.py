"""The dirtiness ladder + impact analysis, staged through the `.clever/` store.

Per source file we climb an escalating ladder, stopping as soon as a cheap rung
acquits the file of dirtiness:

  L0  mtime unchanged                         -> clean, no work
  L1  byte hash unchanged                     -> clean (touch only)
  L2  normalised-source hash unchanged        -> clean (comment/whitespace) --
      (or flags unchanged & AST identical)       NO libclang needed
  L3  AST changed, only abi-only bodies differ-> recompile self; consumers safe
  L4  an exported symbol's signature/removal  -> recompile self AND importers
      or a body-is-interface body changed         that actually use that symbol

Only files that reach the bottom of the cheap rungs pay for a libclang parse.
This module performs NO compilation; it produces a `Report` and writes the
digest / ast / graph artifacts under `.clever/`.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field
from pathlib import Path

from . import comprehension as comp_mod
from . import digest as digest_mod


@dataclass
class FileVerdict:
    rel: str
    level: str
    self_dirty: bool
    summary: str
    impacting: list[tuple[str, str]] = field(default_factory=list)


@dataclass
class Report:
    baseline: bool
    verdicts: list[FileVerdict]
    rebuild: dict[str, str]
    changed: list[str]


def _parse_flags(manifest: dict, repo: Path, target: dict) -> list[str]:
    fl: list[str] = []
    for d in target.get("include_dirs", []):
        fl.append("-I" + str(repo / d))
    for d in target.get("defines", []):
        fl.append("-D" + d)
    cf = target.get("compile_flags", [])
    i = 0
    while i < len(cf):
        if cf[i] == "-isystem" and i + 1 < len(cf):
            fl += ["-isystem", cf[i + 1]]
            i += 2
            continue
        i += 1
    return fl


def _target_index(manifest: dict) -> dict[str, dict]:
    idx: dict[str, dict] = {}
    for t in manifest["targets"]:
        for s in t["sources"]:
            idx.setdefault(s, t)
    return idx


def _transitive_importers(module, provided_by, imports_of) -> set[str]:
    importers_of_module: dict[str, set[str]] = {}
    for rel, imps in imports_of.items():
        for m in imps:
            importers_of_module.setdefault(m, set()).add(rel)
    result: set[str] = set()
    seen: set[str] = set()
    q = deque([module])
    while q:
        mod = q.popleft()
        if mod in seen:
            continue
        seen.add(mod)
        for rel in importers_of_module.get(mod, ()):
            if rel not in result:
                result.add(rel)
                own = provided_by.get(rel)
                if own:
                    q.append(own)
    return result


def _diff_interface(old_ast: dict, new_ast: dict) -> list[tuple[str, str]]:
    out: list[tuple[str, str]] = []
    old_p = old_ast.get("provides", {})
    new_p = new_ast.get("provides", {})
    for usr, s in new_p.items():
        if not s.get("exported"):
            continue
        o = old_p.get(usr)
        if o is None:
            out.append((s["name"], "added"))
        elif o.get("sig_hash") != s.get("sig_hash"):
            out.append((s["name"], "signature changed"))
        elif o.get("body_hash") != s.get("body_hash") and s.get("class") == "body-is-interface":
            out.append((s["name"], f"{s['class']} body changed"))
    for usr, o in old_p.items():
        if o.get("exported") and usr not in new_p:
            out.append((o["name"], "removed"))
    return out


def run(manifest: dict, repo: Path, store, update: bool = True) -> Report:
    tindex = _target_index(manifest)
    std = manifest["compiler"]["std"]
    extra = manifest.get("comprehend", {}).get("extra_args", [])
    rels = [r for r in sorted(tindex.keys()) if (repo / r).exists()]

    baseline = not store.has_any(rels, "digest")

    verdicts: list[FileVerdict] = []
    changed: list[str] = []
    digests: dict[str, dict] = {}
    asts: dict[str, dict] = {}

    for rel in rels:
        path = repo / rel
        target = tindex[rel]
        flags = _parse_flags(manifest, repo, target)
        flags_str = " ".join(flags)

        new_dig = digest_mod.make(path, flags_str)
        prior_dig = store.read(rel, "digest")
        prior_ast = store.read(rel, "ast")

        level = None
        self_dirty = False
        summary = ""
        impacting: list[tuple[str, str]] = []
        ast = prior_ast

        if prior_dig and prior_ast and prior_dig["mtime"] == new_dig["mtime"] \
                and prior_dig["sha256"] == new_dig["sha256"]:
            level = "L0"
        elif prior_dig and prior_ast and prior_dig["sha256"] == new_dig["sha256"]:
            level, summary = "L1", "touched, identical bytes"
        elif prior_dig and prior_ast and prior_dig["norm_hash"] == new_dig["norm_hash"] \
                and prior_dig["flags"] == new_dig["flags"]:
            level, summary = "L2", "cosmetic only (comments/whitespace)"
        else:
            # Earn a libclang parse.
            ast = comp_mod.comprehend(path, flags, std, extra).to_json()
            if not prior_ast:
                level, self_dirty, summary = "NEW", True, "no prior baseline"
                changed.append(rel)
            elif prior_ast.get("provides") == ast.get("provides") \
                    and prior_ast.get("idents") == ast.get("idents"):
                level, summary = "L2", "cosmetic only (formatting)"
            else:
                impacting = _diff_interface(prior_ast, ast)
                self_dirty = True
                changed.append(rel)
                if impacting:
                    why = ", ".join(f"{n} ({r})" for n, r in impacting[:6])
                    tail = "" if len(impacting) <= 6 else f" +{len(impacting)-6} more"
                    level, summary = "L4", f"interface change: {why}{tail}"
                else:
                    level = "L3"
                    summary = "body-only change (abi-only) -- recompile self, no consumers"

        digests[rel] = new_dig
        asts[rel] = ast or {}
        verdicts.append(FileVerdict(rel, level, self_dirty, summary, impacting))

        if update:
            store.write(rel, "digest", new_dig)
            if ast is not None:
                store.write(rel, "ast", ast)

    # --- module graph (from digests) -------------------------------------
    provided_by: dict[str, str] = {}
    module_file: dict[str, str] = {}
    imports_of: dict[str, list[str]] = {}
    for rel, d in digests.items():
        if d.get("provides"):
            provided_by[rel] = d["provides"]
            module_file[d["provides"]] = rel
        imports_of[rel] = d.get("requires", [])

    if update:
        for rel, d in digests.items():
            requires_files = {m: module_file.get(m) for m in d.get("requires", [])}
            mod = d.get("provides")
            consumers = sorted(
                r for r, imps in imports_of.items() if mod and mod in imps
            )
            store.write(rel, "graph", {
                "module": mod,
                "requires": requires_files,
                "consumers": consumers,
            })

    # --- impact analysis -------------------------------------------------
    idents_of = {rel: set(a.get("idents", [])) for rel, a in asts.items()}
    rebuild: dict[str, str] = {v.rel: "changed" for v in verdicts if v.self_dirty}

    if not baseline:
        for v in verdicts:
            if not v.impacting:
                continue
            mod = provided_by.get(v.rel)
            if not mod:
                continue
            importers = _transitive_importers(mod, provided_by, imports_of)
            names = {n for n, _ in v.impacting}
            for rel in importers:
                hits = names & idents_of.get(rel, set())
                if hits and rel not in rebuild:
                    rebuild[rel] = f"uses {', '.join(sorted(hits)[:3])} <- {Path(v.rel).name}"

    return Report(baseline=baseline, verdicts=verdicts, rebuild=rebuild, changed=changed)
