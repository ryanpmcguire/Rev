# Rev.Element — About

The concepts behind `Element`: how the tree thinks, why it redraws, and what "dirty"
means. For the call-level reference, see `Element.Api.md`.

---

## A retained tree with shared state

The UI is a retained tree of `Element`s: you build it once and keep it. Each element
holds a pointer to one `Shared` instance common to the whole tree — the canvas, the
current `Event`, the focused text element, and the dirty work-lists. Per-element state
(`parent`, `children`, `styles`, `resolved`, `rect`, dirty flags) is local.

The guiding pattern: **declare structure once, reflect state continuously.** A widget
builds its children in its constructor and then, each frame, only *adjusts* them to
match data. Rebuilding the subtree every frame is the anti-pattern — it throws away the
retained tree's whole advantage and churns layout.

---

## Understanding dirtiness

Nothing recomputes unless something marked it dirty. There are three kinds, and they
are deliberately separate because they cost different amounts:

- **Restyle** — an element's style inputs changed (a style was edited, a state like
  hover/disabled flipped). The element re-resolves its `resolved.style` from its style
  list. Tracked per-element via a `DirtyFlag` and collected in `shared.dirty.restyle`.
- **Refresh / draw** — an element must repaint. `refresh()` sets the element's draw
  flag, adds it to `shared.dirty.refresh`, and **propagates upward** so ancestors
  repaint too. This is paint only; it does not by itself move anything.
- **Layout** — geometry may have changed, so the whole-tree flex pass must run.
  Gated by a single shared flag, `layoutDirty`: raised by anything that can change
  geometry, then consumed and cleared by the window's draw. While it's false, the flex
  pipeline is skipped entirely and last frame's geometry is reused.

The important relationship: restyle and refresh are cheap and local-ish; layout is the
expensive global pass. So the system works hard to avoid raising `layoutDirty`.

---

## The suspicion gate

When a restyle happens, the element compares the new resolved style against the old and
asks a narrow question: *did anything that affects geometry actually change?* Only then
is `layoutDirty` raised. A pure-paint restyle — a hover background, a border color —
re-resolves the style and repaints, but **skips the flex pass** for that frame.

This is the core efficiency idea: a style change is treated as suspicious of moving
geometry, and must prove it before triggering a relayout. Transitions that animate a
geometric value stay "sticky" — they keep forcing relayout until they finish, so a
later pure-paint restyle in the middle can't accidentally freeze the animation.

---

## Resolution and the cascade

Styles are declarative *inputs*; `resolved` is the computed *output*, rebuilt each
frame from the applicable styles (ordered list + per-element override) under the current
state flags (hover/press/focus/disabled). Never treat raw style values as live truth —
read `resolved`, and treat `rect`/`resolved` as outputs you don't write.

Some properties cascade top-down (a separate pass): an element is hidden if it or any
ancestor is hidden; disabled if it or any ancestor is disabled (so disabling a section
disables its subtree); and depth is derived from the parent's depth and the element's
z-index. Cascade runs before per-element resolution so the inherited state is known.

---

## The per-frame pipeline

A frame walks the dirty elements through a fixed sequence of overridable hooks:
`computeChildren` (reflect data into the child set) → `computeStyle` (reflect data into
styles) → style resolution + cascade → the layout flex pass (only if `layoutDirty`) →
`computePrimitives` (reflect style/data into draw primitives) → `stencil` → `draw`.

A widget participates by overriding the relevant hooks, not by driving the pipeline.
The window owns the ordering and timing; an element never calls these on itself.

---

## Events and target flags

Input flows down the tree through the event virtuals (`mouseDown`, `mouseMove`, …),
which also perform hit-testing and decide what propagates to which children. Each
element carries per-frame `targetFlags` (`hit`, `hover`, `press`, `focus`, `drag`,
`disabled`) describing its relationship to the current event — these are inputs you
read to reflect state, not values you set (except disabled, via `setDisabled`).

Behavior is wired either by registering a callback (`on*`) or by overriding the virtual.
Overriding replaces default propagation, so an override that still wants the defaults
calls its base first.

`disabled` is intent, not enforcement: it cascades and re-resolves styles, but each
control decides what disabled *means* behaviorally.

---

## Animation

Transitions live per-element as a list of in-flight float eases, stepped by the animate
pass against the event clock. An element with active transitions keeps requesting
refresh so it continues to draw; a transition on a geometric value keeps the layout
dirty until it drains. The first draw of an element never animates — there's no prior
value to ease from.

---

## A note on the layout engine

The flex/layout engine (minima/maxima resolution, row packing, grow passes, rect
placement) is a complete, constraint-correct realization of the layout model — not a
rough draft. Its *internals* are still an implementation, though: treat it as a black
box driven by the window, don't call into it, and don't depend on its intermediate
fields. What's stable is the behavior it computes (see "Layout" in `Rev.Style.md`)
along with the surrounding model — dirtiness, the suspicion gate, resolution.

The one thing worth knowing about its shape: sizing is a two-direction pass, because
that is what the problem actually requires. Minima are knowable only **bottom-up** (a
box's smallest size depends on its content's smallest size); maxima are dictated
**top-down** (how much room a child may take depends on the space its parent has). Every
dimension therefore carries a resolved `min`/`val`/`max` triple all the way through, and
the grow pass is a real constraint solve over those bounds — not a single proportional
split. This is why Rev's layout avoids the circular-sizing and "it almost fits" failures
flexbox is prone to; the details are in `Rev.Style.md`.
