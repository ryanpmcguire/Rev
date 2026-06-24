# Rev — Dos and Don'ts

The concentrated checklist. Each line has a *why*, because the why is what lets you
generalize to a case not listed here. If a habit below contradicts something you were about
to do, the habit is probably right — Rev punishes over-doing far more than under-doing.

---

## Structure & ownership

- ✅ **Derive from `Box`** when your element's own root paints (background/border/clip);
  from bare **`Element`** when its root paints nothing and the visible parts are children.
  *Why:* `Box` carries a `Rectangle` primitive; bare `Element` is free of it.
- ✅ **Build children and primitives once, in the constructor.** Keep typed pointers to the
  ones you'll talk to. *Why:* the tree is retained; rebuilding churns layout and discards
  its only advantage.
- ✅ **Attach a child by passing `parent`** (`new Text(this, ...)`). The parent owns it.
  *Why:* the subtree owns its nodes; you don't free them.
- ❌ **Don't `new` children inside `compute*`/per frame.** *Why:* that's rebuilding, not
  reflecting.
- ❌ **Don't `delete` a child you attached, or keep pointers across an ancestor's
  deletion.** *Why:* the parent's destructor frees the subtree — double-free / dangling.
- ❌ **Don't name your window subclass `Window`.** Name it `AppWindow`. *Why:* sharing an
  unqualified name with `Rev::Window` is an ODR/type-identity hazard that can abort teardown
  on close.

## State, reflection & redraw

- ✅ **Mutate the single owner (model/`AppState`/device) directly in a handler, then
  `refresh(e)`.** *Why:* `refresh` makes the branch reconsider children + styles + data
  next frame; that's the whole contract.
- ✅ **Keep one owner for each truth.** The GUI editing that owner is fine. *Why:*
  "reflect, don't push" means *one source of truth*, not "the GUI may never write."
- ✅ **Read `resolved`, `rect`, `targetFlags`** to reflect computed state. *Why:* they are
  per-frame outputs.
- ❌ **Don't write `resolved`/`rect`.** *Why:* recomputed every frame; your write is lost
  and you've misunderstood the dataflow.
- ❌ **Don't read raw `Style` values expecting persistence.** *Why:* styles are *inputs*
  re-resolved each frame; the truth is `resolved`.
- ❌ **Don't build an observer graph for ordinary click→state→redraw.** *Why:* `refresh`
  already covers it; the graph is ceremony.

## Events

- ✅ **Use `child->on*(...)`** to compose children; **override the virtual** for your
  element's *intrinsic* behavior. *Why:* the virtual is the real entry point (it drives
  hit-testing and propagation); a listener-on-self is a weaker parallel path.
- ✅ **Call the base in an overridden virtual** unless you mean to halt defaults. Base
  **last** → self-then-children (top-down); base **first** → children-then-self
  (bottom-up). *Why:* the base does propagation + flag bookkeeping; placement sets ordering.
- ✅ **Read `targetFlags` for input state** (`press`/`hover`/`focus`/`hit`/`drag`). *Why:*
  the framework keeps them honest; they ARE the distributed target info.
- ✅ **Set `e.propagate = false`** to stop an event (Rev's `stopPropagation`).
- ❌ **Don't hand-track hover/press/focus** with manual down/leave bookkeeping. *Why:*
  you'll fight click-cancel-on-drag-off and the focus chain; the flags already encode it.
- ❌ **Don't reconstruct typed text from `keyDown`.** Use `textInput`/`keyboard.input`.
  *Why:* dead keys / IME / layout — the OS emits the *character* separately.
- ❌ **Don't store the `Event&`** beyond the handler. *Why:* it's a reused, reset snapshot.
- ❌ **Don't expect a single focused element.** *Why:* focus is a *chain*; a container and
  its child are focused together by design.

## Performance gates (Observable / dirty flags)

- ✅ **Default to `refresh`; add a gate only at a known-costly spot,** to *skip* work.
  *Why:* gating is pruning, not architecture.
- ✅ **Gate animated `compute*`** that does real work. *Why:* animation refreshes every
  frame by design; separate redraw (cheap) from recompute (expensive).
- ✅ **Use `Observable` for out-of-band/cross-thread changes** that have no `Event&`
  (telemetry, async load) — set a dirty flag, bump a refresh on the main thread. Always
  `unsubscribe` the owner in its destructor. *Why:* there's no handler to `refresh` from,
  and a destroyed subscriber must not get a dangling call.
- ❌ **Don't gate defensively everywhere.** *Why:* premature gating is over-engineering one
  level down.

## Layout & style

- ✅ **`prepend` your default styles** so caller-supplied styles (added after) win. *Why:*
  a `StyleList` applies in order; later overrides earlier.
- ✅ **Use typed `Dist`** (`14_px`, `50_pct`, `Grow()`, `Shrink()`). *Why:* compiler-checked,
  no string parsing.
- ✅ **Remember spacing is left-right-top-bottom**, not CSS clockwise. `{14,14,14,14}` is
  L/R/T/B. *Why:* the framework's convention everywhere (`Lrtb`).
- ✅ **Override `computeLayout`** only for self-measured content (e.g. text), and raise
  `shared->layoutDirty` when its content minima move. *Why:* content minima live outside
  `resolved.style` and slip past the style-diff gate.
- ❌ **Don't rely on `_pct` to size a content-sized parent.** *Why:* relative dimensions are
  top-down only; they overflow rather than inflate the parent (the percentage feedback loop
  is gone by construction).

## Graphics (when you reach Rung 4–5)

- ✅ **One pipeline per primitive *type*** (share via `Rev.Core.Shared`); per-instance data
  in a mapped buffer — **writing through `data` IS the upload.** *Why:* instancing; no
  staging.
- ✅ **Supply shaders per backend** in the `Pipeline` params; use `DEFINITIONS` for variants.
  *Why:* the backend is chosen by the build, not your code.
- ✅ **Clip via the stencil** (`overflow: hide` / `stencilPush`/`Pop`). *Why:* nesting is
  free and arbitrarily deep; there is no clip-rect stack.
- ❌ **Don't call `glXxx` from an element**, assume the backend, `glBufferSubData` a mapped
  buffer, make a pipeline per instance, or create GPU resources without a live context.
  *Why:* each breaks the layering or the design's fast path.

## Build (Clever) — the ones that bite

- ✅ **Always `transpile --config debug` (or the shorthand).** *Why:* the CRT lives in the
  config; a config-less build link-fails with `mainCRTStartup`/`__imp_` noise.
- ✅ **Treat a link "permission denied" on the `.exe` as "the app is running,"** not a code
  error. *Why:* it's a file lock; close the app and relink.
- ✅ **Edit the `.ixx`; errors point there via `#line`.** *Why:* `.clever/` is generated and
  overwritten.
- ❌ **Don't edit `.clever/`, run `clever init` on a tuned manifest, or `rm -rf .clever`
  "to be safe."** *Why:* generated / destructive / wastes a correct incremental build.

---

## The meta-rule

When in doubt, **do less and trust the framework.** Almost every Rev bug an agent
introduces is from doing too much — rebuilding instead of reflecting, observing instead of
refreshing, hand-tracking instead of reading a flag, gating instead of measuring. The
idiomatic Rev solution is usually the *smaller* one.
