# Rev.Element.Event — About

`Rev::Element::Event` is the single input snapshot threaded through the whole element
tree. One `Event` lives on the tree's shared state; it is passed by reference into every
event virtual and every `on*` callback. Understanding it is mostly understanding one
idea: **it is a live snapshot of input state, not a one-shot message.**

## It is state, not a message

A traditional UI event says "a left-arrow key was just pressed." Rev's `Event` instead
holds the *current* state of the mouse and keyboard, plus a little timing. When your
handler runs, you read the fields you care about to decide what to do. So:

```cpp
if (e.keyboard.arrows.left) { ... }   // is LEFT held right now?
```

reads whether left is currently down — not whether a left press just occurred. This is
why per-frame drivers (e.g. the jog animator) read the same `Event` each tick and act on
the live state.

The same object is reused for every dispatch. Read it inside your handler; never store
the `Event&` (or a pointer to it) to look at later — by then it has been reset and
refilled for another dispatch.

## `Button` — a sticky press state

Mouse buttons, modifiers, and arrows are all `Button`s. A `Button` is a small state
machine, not a pulse:

- `operator bool()` — true while held. `if (e.mouse.lb)` means "left mouse is down."
- `id` — positive while pressed, negative while released; its magnitude counts presses.
- `isDoubleClick(timeThresh = 200ms, lenThresh = 10px)` — Rev's cross-platform
  double-click test, true when the last two presses were close enough in time *and*
  position. (It tracks `lastPressTime`/`lastPressPos` and the diffs for you.)

## `mouse`

- `pos` — cursor position in this window. `down` / `up` — where the last press/release
  landed.
- `screenPos` — absolute physical-pixel cursor position from the OS, independent of the
  window origin (so drag/resize math stays stable while the window moves).
- `drag`, `diff`, `dragStart`, `dragEnd` — drag deltas and endpoints.
- `wheel` — scroll delta.
- `lb`, `mb`, `rb` — left / middle / right `Button`s.
- `cursor` — the cursor shape request for this frame (reset to `Unset` each dispatch; a
  handler may set it to ask for a different cursor).

## `keyboard`

- Modifiers as `Button`s: `ctrl`, `alt`, `shift`.
- Named keys as `Button`s: `escape`, `tab`, `del`, `backspace`, `enter`, `space`.
- `arrows` — `left`, `right`, `up`, `down`, each a `Button`.
- `key` — the name of the key in play.
- `input` — committed text for text input (what a `TextInput` consumes); not a held-state
  field.

## Event-level fields

- `propagate` — set `false` to stop the event going further (Rev's `stopPropagation`).
  Reset to `true` before each dispatch.
- `causedRefresh` — flagged when handling this event marked something for redraw.
- `time` (and `firstTime`) — the millisecond clock for this dispatch; this is what
  transitions and any time-based handling read.
- `id` — a dispatch id.
- `canvas` — the draw surface for this dispatch.

`resetBeforeDispatch()` refreshes `time`, re-arms `propagate`, clears `causedRefresh`, and
resets the cursor request. The framework calls it; you don't.

## How not to use Event

- Don't treat it as "what just happened." It's the current state; combine it with your
  element's `targetFlags` (hit/hover/press/…) to decide relevance.
- Don't stash the `Event&` or a pointer to it beyond the handler — it is reused and reset.
- Don't write to the mouse/keyboard fields; read them. The one field you set is
  `propagate` (to stop), and `mouse.cursor` (to request a cursor).
- Don't poll `key`/`input` to detect a held key — use the `Button` fields. `input` is
  committed text, not live key state.
