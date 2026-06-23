# Rev

Rev is a general-purpose desktop application framework for C++ — a full stack from the
native window down to the pixels. In spirit it's a from-scratch reimplementation of the
web platform (a DOM-like element tree, CSS-like styling, flexbox-like layout, an event
model, retained UI) but **C++-native**, with the goal of keeping the good ideas of the
web while removing its jank: no string parsing, no hidden reflow, explicit types, and
explicit control over when work happens.

## The stack, top to bottom

- **Native** — window creation and the OS event/message loop (`Rev.Application`,
  `Rev.NativeWindow`, `Rev.Window`).
- **Graphics** — a canvas and command buffer over the GPU (`Rev.Graphics.*`,
  OpenGL today) and drawable primitives (`Rev.Primitive.*`).
- **Elements** — the retained UI tree and widgets (`Rev.Element`, `Rev.Element.Box`,
  `Rev.Element.Text`, the controls). See `Element/About/`.
- **Appearance** — the style and layout model (`Rev.Appearance`). See `Rev.Style.md`.
- **Core** — geometry, color, the event dispatcher, dirty tracking, the frame
  scheduler, time (`Rev.Core.*`). See `Rev.Core.md`.

## How Rev is similar to React / CSS

- **A retained element tree** is the DOM. Every widget derives from `Element`; parents
  own their children.
- **Styles are declarative and cascading**, like CSS: shared style objects applied in
  order, with state variants (hover / press / focus / disabled) and inheritance.
- **Layout is in the flexbox family**: an axis, main- and cross-axis alignment, wrapping,
  and growable/shrinkable sizes — but it carries flexbox's model to completion (a real
  `min`/`val`/`max` constraint solve, growable margins, no percentage feedback loops).
  See `Rev.Style.md`.
- **Events are listeners**: `element->onClick([]{...})` is `addEventListener`.
- **Widgets are components**: subclass an element, build structure once, reflect state —
  analogous to a React component's render, but explicit.
- **Transitions** animate style properties over time, like CSS transitions.
- **Reconciliation via dirtiness**: changes mark the tree dirty and only the affected
  parts recompute — the manual, explicit cousin of React's re-render.

## What Rev does differently

- **Event propagation is top-down by default.** A base event virtual fires the
  element's own listeners first, then recurses to children — so a parent handles before
  its children. You can **flip an element to bottom-up** by calling the base virtual
  *first* in your override (children run before your code) instead of *last* (your code
  first, the default). The web's fixed capture-then-bubble is replaced by per-element
  control. Set `e.propagate = false` to stop propagation (like `stopPropagation`).
- **Spacing is left-right-top-bottom, not clockwise.** `margin`/`padding`/`pos` are
  `{ left, right, top, bottom }`. The 4-argument form `{14, 14, 14, 14}` is L/R/T/B —
  **not** CSS's top-right-bottom-left shorthand. (See `Rev.Core.Lrtb`.)
- **Units are typed, not strings.** Lengths are a `Dist`: `14_px`, `50_pct`, `Grow()`,
  `Shrink()` — no `"14px"` parsing, and the compiler checks them.
- **No DOM diffing.** You build the tree in C++ and *reflect* live state into it each
  frame; you do not return new virtual trees to be reconciled. The pattern is "declare
  structure once, reflect state."
- **Dirtiness is explicit and tiered** — restyle, refresh/draw, and layout are separate,
  and a "suspicion gate" lets a pure-paint change (e.g. a hover color) skip the layout
  pass entirely. See `Element/About/Element.About.md`.
- **Resolution each frame**: styles are *inputs*; `resolved` is the computed *output*.
  You read `resolved`/`rect`, you don't write them.

## Where to read more

- `Rev.Core.md` — the core utilities (geometry, dispatcher, dirty flags, scheduler).
- `Rev.Style.md` — the appearance/layout model in detail.
- `Element/About/Element.Api.md` and `Element.About.md` — the element tree.
