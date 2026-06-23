# Rev.Core

The `Rev::Core` layer is the framework's foundation: geometry, color, the event
dispatcher, dirty tracking, and the frame scheduler. These have no dependency on the UI
tree and are reused throughout the stack (and by application code).

## Geometry

- **`Rev.Core.Pos`** — a 2D position/vector (`x`, `y`) with the usual arithmetic. The
  general-purpose 2D value.
- **`Rev.Core.Pos3`** — the 3D counterpart (`x`, `y`, `z`), for the 3D view and machine
  geometry.
- **`Rev.Core.Rect`** — an axis-aligned rectangle (`x`, `y`, `w`, `h`) with helpers like
  `contains(pos)`. Elements' resolved geometry is a `Rect`.
- **`Rev.Core.Lrtb`** — a box of four edges: `left, right, top, bottom`. This ordering is
  the framework's convention everywhere (margins, padding, insets) — **not** CSS's
  clockwise shorthand. `span()` returns the `(r-l, b-t)` extent.

## Color

- **`Rev.Core.Color`** — color values and the `rgba` / `rgb` / `Tint` helpers used in
  styles. (The style layer re-exports its own `sColor`; see `Rev.Style.md`.)

## Events & reactivity

- **`Rev.Core.Dispatcher`** — a typed publish/subscribe channel keyed by a member-function
  pointer. `listen(&T::method, fn)` registers; `tell(&T::method, e)` fires all listeners
  for that key. Subscribers may attach with an owner pointer so they can `unsubscribe`
  all of their callbacks at once (used widely so a destroyed subscriber never gets a
  dangling call). This is what backs `Element`'s `on*` handlers.
- **`Rev.Core.DirtyFlag`** — a flag that can `subscribe` to other flags and run an
  `onDirty` callback when set, so marking one thing dirty can ripple to dependents. The
  element style system is built on these.
- **`Rev.Core.Observable`** — a value that announces changes to observers; useful for
  "store the truth in one place, let views reflect it."

## Time & scheduling

- **`Rev.Core.Process`** — the cooperative scheduler. `schedule(owner, intervalMs, cb)`
  asks for a repeating tick; `unschedule(owner)` stops it; the native loop calls `tick()`
  and `msUntilNextTick()` decides how long to sleep. One schedule per owner (a new
  schedule replaces the previous for that owner) — use distinct owner pointers for
  distinct cadences. This drives animation, polling, and any time-based work.
- **`Rev.GlobalTime`** — the shared clock; `GlobalTime::now` is the current time in ms,
  refreshed each loop. Use it when you need "now" without threading a value through.

## How not to use Rev.Core

- Don't busy-wait or sleep on your own thread for timing — `schedule` a `Process` tick.
- Don't give two unrelated repeating tasks the same `Process` owner — the second replaces
  the first. Key them on distinct pointers.
- Don't assume `Lrtb`/spacing is clockwise; it is left, right, top, bottom.
