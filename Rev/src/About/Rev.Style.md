# Rev.Style (Rev.Appearance)

The appearance layer (module `Rev.Appearance`) is Rev's CSS: a declarative style model
plus a layout model in the flexbox *family* that finishes the job flexbox starts (see
"Layout" below). Styles are *inputs*; each frame an element resolves its applicable
styles into a computed output (`resolved`). You read the output, you don't write it.

## Lengths: `Dist`

Every size/length is a typed `Dist`, not a string:

- `14_px` / `Px(14)` — absolute pixels.
- `50_pct` / `Pct(50)` — relative to the parent (a fraction).
- `Grow()` — take a share of leftover space (flex-grow).
- `Shrink()` — give up space when short.
- Unset (the default) — fit the children: be as small as allowed while still containing
  them (shrink-to-fit). This is the baseline sizing, not "no size."
- Inherit — take the parent's value.

Because they're typed, the compiler checks them; there is no `"14px"` parsing.

Unlike CSS, a `Dist` is a *kind* of length, not a value with a separate `flex-grow`/
`flex-shrink`/`flex-basis` triad bolted on. "Grow" and "shrink" are just two of the
kinds a width or a *margin* can be, so they compose with `min`/`max` the same way pixels
do, and the engine treats them uniformly (see "Layout"). There is no `flex-basis`
vs. `width` ambiguity to reconcile — a dimension has exactly one declared kind.

## Color

`sColor` with helpers `rgba(r,g,b,a)`, `rgb(r,g,b)`, and `Tint(color)`. Used for
backgrounds, borders, text, shadows.

## Spacing is left-right-top-bottom

`margin`, `padding`, and `pos` are `LrtbStyle` — four independent `Dist`s named
`left, right, top, bottom`, each with optional `min`/`max`. The brace form follows that
order:

```cpp
.padding = { 14_px, 14_px, 14_px, 14_px },   // L, R, T, B  (NOT clockwise)
.margin  = { .top = 4_px, .bottom = 4_px },   // or set named edges
```

Assigning a single `Dist` sets all four edges. This is the most common point of
confusion for anyone coming from CSS — there is no top-right-bottom-left shorthand.

## Size

`Size { Dist width, height; }` with `min`/`max` variants. Combine with `Grow()` for flex
behavior:

```cpp
.size = { .width = 100_pct },     // fill parent width
.size = { .width = Grow() },      // share leftover space
.size = { .width = Grow(), .min = { .width = 120_px }, .max = { .width = 400_px } },
```

`min`/`max` are not an afterthought — they are carried as a resolved `min`/`val`/`max`
triple through every stage of layout (see "Layout"), so a bound you declare is honored
during growing and shrinking, not just clamped at the end. A `Grow()` element that hits
its `max` stops growing and hands its leftover share back to its still-growing siblings;
a `min` is a floor the shrink pass will not cross. This is the constraint behavior CSS
flexbox specifies but applies unevenly — here it is the whole mechanism.

## Layout (the flexbox family, finished)

A container's `layout` selects how children are placed:

- **`Axis`** — `Horizontal` or `Vertical` (the main axis).
- **`Align`** — `Start`, `End`, `Center`, `SpaceAround`, `SpaceBetween` (main-axis
  distribution).
- **`Wrap`** — `True` / `False` (and text-specific `BreakChar`/`BreakWord`/`BreakLine`).
- **`CrossAlign`** — whether members align within their row on the cross axis.

```cpp
.layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False }
```

`Position` (`Relative`/`Absolute`) and `zIndex` control out-of-flow placement and depth.

If you know flexbox, you already know the shape of this: an axis, alignment, wrapping
into rows, and growable children. What follows is where Rev goes past it. Flexbox has a
correct *model* buried under a confused API and a handful of well-known footguns; Rev
keeps the model and removes the rest.

### Sizing is one two-direction pass over `min`/`val`/`max`

Every dimension resolves a `min`/`val`/`max` triple, and the engine fills it in the only
order that is actually well-posed: **minima bottom-up** (a box's smallest size depends on
its content), **maxima top-down** (a child's ceiling depends on the room its parent has).
Because both bounds are known before placement, growing and shrinking are a genuine
constraint solve, not a single proportional divide:

- A `Grow()` member that reaches its `max` **freezes** and its leftover share is
  redistributed among the members that can still take it — so space is never left on the
  table and never overflows a declared ceiling. (Flexbox specifies this; Rev does it
  plainly and consistently.)
- A `min` is a hard floor the shrink pass will not cross.

### Growth is uniform across sizes *and* margins

Growing is a property a width, a height, **or a margin** can have — it's the same
primitive everywhere. So the things CSS makes you reach for special cases to do fall out
for free:

- `margin` of `Grow()` is the generalization of `margin: auto` — push an item to one end,
  center it, or split a gap, with the *same* mechanism as a growable size. There is no
  separate "auto margin" rule to remember.
- Main-axis distribution (`Align`) and growable margins are the same idea applied at
  different granularity, so "space it out" and "let it grow" don't fight.

### Relative sizes can't inflate their parents (the percentage footgun, gone)

In CSS, a percentage-sized child whose parent is itself content-sized creates a circular
dependency — the parent sizes to the child, the child to the parent — and you get
collapse, jitter, or surprise overflow. Rev cuts the loop by construction: a relative
(`_pct`) dimension is **parent-dictated, top-down only**. It never contributes to its
parent's content-derived minimum; if it doesn't fit, it overflows rather than pushing the
parent outward. Only genuinely content-sized dimensions (unset / `Grow()`) infer upward.
So "% of the parent" always means exactly that, with no feedback loop.

### One declared kind per dimension

There is no `width` vs. `flex-basis` vs. `min-width` precedence puzzle. A dimension has a
single `Dist` kind plus optional `min`/`max`, and that is the whole story the engine
reads. Less expressive in the corners than CSS, far more predictable in practice.

## Paint

- **`Background`** — `{ .color = ... }`.
- **`Border`** — `{ .color, .radius, .width }` (radius/width are `Dist`).
- **Text** — `{ .color, .size, .lineHeight, .spacing }`.
- Shadow, `Cursor` (hand/not-allowed/…), and `Visibility` (visible/hidden) round it out.

## State variants and combination

A style can declare it only `applies` in a state — hover, press, focus, disabled:

```cpp
static inline Style BtnHover = { .applies = { .hover = true }, .background = {...} };
```

An element holds a `StyleList` (shared style objects, **applied in order** so later ones
win) plus a private per-element override layer. Each frame the element applies the ones
whose `applies` match its current state, then its override, producing `resolved.style`.

## Transitions

A property can ease to its new value over time. Durations are `_ms` / `_sec`, and a
property carries an optional transition length; the element's animate pass interpolates
each frame until done.

```cpp
.background = { .color = rgba(...), .transition = 120_ms }
```

## How not to use Rev.Style

- Don't use CSS clockwise spacing order — it's left, right, top, bottom.
- Don't pass string units — use `Dist` literals (`_px`, `_pct`, `Grow()`).
- Don't read or mutate `resolved` as if it were your style input; it's the per-frame
  output. Set styles; read `resolved` only to observe the computed result.
- Don't rely on style-object order being ignored — later styles in the list override
  earlier ones.
