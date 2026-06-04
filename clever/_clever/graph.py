"""The module dependency graph.

Nodes are translation units. Edges come from `import` statements resolved
through the module-name -> providing-file map. The graph gives us:

  * a topological order for interface units (a module must be compiled before
    anything that imports it),
  * the transitive set of modules each unit needs (clang's explicit-module
    mode wants `-fmodule-file=` for the whole closure, not just direct
    imports),
  * the reverse edges, used only for reporting which consumers a change would
    have hit.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from .scan import ScanResult


@dataclass
class Node:
    source: Path
    target: str
    provides: str | None
    requires: list[str]          # direct module imports
    is_module: bool


@dataclass
class Graph:
    nodes: dict[Path, Node] = field(default_factory=dict)
    provider: dict[str, Path] = field(default_factory=dict)  # module name -> file

    def closure(self, source: Path) -> list[str]:
        """All modules transitively required by `source` (names)."""
        seen: set[str] = set()
        stack = list(self.nodes[source].requires)
        while stack:
            name = stack.pop()
            if name in seen:
                continue
            seen.add(name)
            prov = self.provider.get(name)
            if prov is not None:
                stack.extend(self.nodes[prov].requires)
        return sorted(seen)

    def topo_modules(self) -> list[Path]:
        """Topologically ordered interface-unit source paths.

        Raises on a cycle (modules cannot be mutually importing).
        """
        order: list[Path] = []
        state: dict[Path, int] = {}  # 0=visiting, 1=done

        def visit(src: Path, chain: list[str]) -> None:
            st = state.get(src)
            if st == 1:
                return
            if st == 0:
                cyc = " -> ".join(chain)
                raise RuntimeError(f"Module import cycle detected: {cyc}")
            state[src] = 0
            node = self.nodes[src]
            for name in node.requires:
                prov = self.provider.get(name)
                if prov is not None and prov != src:
                    visit(prov, chain + [name])
            state[src] = 1
            order.append(src)

        for src, node in self.nodes.items():
            if node.is_module:
                visit(src, [node.provides or str(src)])
        return order

    def consumers_of(self, module_name: str) -> list[Path]:
        return [s for s, n in self.nodes.items() if module_name in n.requires]


def build_graph(
    units: list[tuple[Path, str, bool]],
    scans: dict[Path, ScanResult],
) -> Graph:
    g = Graph()
    for source, target, is_module in units:
        sr = scans[source]
        g.nodes[source] = Node(
            source=source,
            target=target,
            provides=sr.provides,
            requires=list(sr.requires),
            is_module=is_module,
        )
        if sr.provides:
            # Last writer wins; a clean project has exactly one provider per name.
            g.provider[sr.provides] = source
    return g
