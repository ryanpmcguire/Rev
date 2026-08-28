# Rev.Element — Events

The companion to `Element.Event.About.md`. That file explains the **`Event` object** — the
single live input snapshot threaded through the tree. *This* file explains the **event
system**: how an OS keypress or mouse wiggle becomes a method call on the right element,
how propagation works, and — the part worth slowing down for — *why* each event behaves
the way it does. Click, focus, hover, drag, and key handling each have a small rule that
looks fiddly until you see the problem it solves. We'll do the problems.

If you've used the DOM, you'll find a lot that rhymes and a few things deliberately
changed. Where Rev diverges, we'll say why.

---

## The 10,000-foot view

Every frame that has input, the same four-beat dance plays out (`Window.ixx`):

1. **The OS hands the window a raw event** — "left button went down at screen (812, 440)",
   "the Right arrow is now held", "a character `é` was committed."
2. **The window updates the `Event` snapshot** — it writes the new mouse position, sets
   the relevant `Button` state, etc. The `Event` is *state*, not a message (see
   `Element.Event.About.md`), so this step is "update the world," not "make a packet."
3. **The window hit-tests** (`setTargets`) — it figures out which elements the cursor is
   over *right now* and stamps a per-frame `targetFlags.hit` on them.
4. **The window calls the matching virtual on the root element** (`mouseDown`, `keyDown`,
   …), and that call **propagates down the tree** under rules specific to the event.

The root element *is* the `Window` (it derives from `Element`), so "dispatch to the root"
just means the window calls the virtual on itself and lets it ripple down. After dispatch,
if anything asked to repaint (`e.causedRefresh`), the window refreshes.

That's the whole loop. Everything below is detail on beats 3 and 4.

---

## `targetFlags`: the element's relationship to *this* event

Before any handler runs, each element carries a little bundle of per-frame booleans
describing how it relates to the current input (`Element.ixx`):

```cpp
struct TargetFlags {
    bool hit;       // the cursor is over this element this frame
    bool hover;     // the cursor is over it AND was last frame (sticky)
    bool press;     // a mouse button went down on it and hasn't released yet
    bool focus;     // it (or its subtree) holds keyboard focus
    bool drag;      // a press started here and the mouse may be dragging
    bool click;     // (reserved)
    bool disabled;  // it or an ancestor is disabled
};
```

**Read these; don't write them** (except `disabled`, via `setDisabled`). They are *inputs*
you reflect, not switches you flip. The whole event system is really just the machinery
that keeps these flags honest, and most of your handlers are just asking "what is my
relationship to this event?" and acting on the answer.

The key mental shift from the DOM: there is no `event.target` you compare against. Instead
*each element already knows* whether it's the hit element, whether it's pressed, whether it
holds focus. The flags are the target information, distributed.

---

## Hit-testing: who is under the cursor?

Each input frame the window recomputes hits from scratch (`setTargets` in `Window.ixx`):

1. Clear `hit` on everyone.
2. Walk the elements in **draw order** and ask each `contains(e.mouse.pos)`.
3. For every element that contains the point, **mark the whole ancestor chain** as hit
   (`markHitChain` walks `parent` pointers up to the root).

That third step is the one to internalize: **hit is not just the topmost leaf — it's the
leaf and every ancestor that geometrically contains the point.** A button inside a row
inside a panel: pressing the button marks the button, the row, the panel, and the window
all as `hit`. This is what makes propagation work later — a parent can meaningfully ask
"did this event land somewhere inside me?" by reading its *own* `hit` flag.

### `contains` — the hit shape

By default `contains(pos)` is `rect.contains(pos)` — a rectangle test. Override it for a
non-rectangular element (a circular knob, a 3D viewport that ray-casts). It's the one
place hit *geometry* is decided.

### `interceptHits` — the input shield

Draw order matters because of overlap. If a translucent overlay is painted on top of a
toolbar, you usually don't want clicks falling through to the buttons beneath it. Set
`interceptHits = true` on the overlay: when the cursor is over it, elements painted
*underneath* (earlier in draw order) are denied `hit` for that frame. It's the explicit
version of "this thing eats pointer events." Without it, every overlapping element under
the point would be hit at once.

---

## The propagation pattern (learn this once, it's everywhere)

Almost every event virtual has the same shape. Here's `mouseMove`, lightly trimmed:

```cpp
virtual void mouseMove(Event& e) {
    dispatcher->tell(&Element::mouseMove, e);   // 1. fire MY listeners
    if (!e.propagate) { return; }               // 2. someone said "stop"

    for (Element* child : std::views::reverse(children)) {  // 3. offer to children
        if (child->targetFlags.hit) { child->mouseMove(e); }
        if (!e.propagate) { return; }
    }
}
```

Three ideas, and they recur in every event:

- **Top-down by default.** An element runs *its own* handlers first, then recurses into
  children. So a parent sees the event before its children do. (The DOM's fixed
  capture-then-bubble is replaced by this; you get bubble-like behavior by calling the
  base *first* in an override — see "Override vs. listen.")
- **Children are filtered, not blasted.** The parent only recurses into the children the
  event is *relevant* to — for `mouseMove`, the children that are `hit`. This is why the
  hit-chain matters: it's the routing table.
- **`e.propagate = false` stops the walk** — Rev's `stopPropagation`. The moment a handler
  clears it, the current element returns and no further elements are visited.

- **Reverse order.** Children are visited back-to-front (`std::views::reverse`) so the
  element painted *on top* — the one the user actually sees and means — gets first refusal.

Once you see this skeleton, each specific event is just "what's the filter, and what extra
bookkeeping does it do before recursing?"

---

## Mouse movement, and the birth of hover

The OS only ever tells you "the mouse is now *here*." It never says "the mouse entered the
button" — that's a *derived* fact, and Rev derives it.

During `mouseMove`, a parent compares each child's **current** `hit` against its
**previous** `hover` and synthesizes the transition (`Element.ixx`):

```cpp
if ( containsEvent && !isHoverTarget) { child->mouseEnter(e); }  // crossed in
if (!containsEvent &&  isHoverTarget) { child->mouseLeave(e); }  // crossed out
if ( containsEvent)                   { child->mouseMove(e);  }
```

- `mouseEnter` sets `hover = true`; `mouseLeave` sets it back to `false`.
- `hover` is therefore **sticky** — it persists across frames until the cursor actually
  leaves — whereas `hit` is recomputed fresh every frame. That difference is the whole
  point: `hover` is "is the cursor on me *now*, having been tracked," and it's what drives
  `:hover` style variants.

This is also where the **cursor shape** is resolved: as `mouseMove` walks down to the
innermost hit element, each element that declares a `cursor` writes it into
`e.mouse.cursor`, and the window applies the last one set. Innermost wins because it's
visited last.

> **Why derive enter/leave instead of asking the OS?** Because "entered" is relative to
> *your* element tree and *your* layout, which the OS knows nothing about. The only source
> of truth for "is the cursor inside this box" is the box's resolved rect, which only Rev
> has. So Rev computes it.

---

## Press, release, click, drag — the four-way knot

This is the cluster people get wrong, so let's build it up from what a "click" actually
*is*.

A click is not "a press." A click is **"a press and a release that both belong to the same
element."** The web taught everyone the muscle memory: press a button, change your mind,
slide off, release — *no click*. Slide back on, release — click. That cancel-by-moving
behavior is a feature, and it falls directly out of how the flags are kept.

### Press: set on the way down

`mouseDown` is only delivered to a child that is `hit` (you can't press what you're not
over). When it fires, the element sets two flags **before** notifying its own listeners:

```cpp
targetFlags.drag  = true;   // a gesture started here
targetFlags.press = true;   // this element is now "held"
```

Setting them first matters: by the time your `onMouseDown` callback runs, `press` is
already `true`, so a handler that reads press flags sees the correct state.

### Release: the capture trick

Here's the subtle bit. On release you want two different things:

- **`press` must clear no matter where the cursor is.** If you press a button and release
  out in space, the button must un-press. A stuck "held" button is a bug.
- **`click` must fire only if you release back *over* the element.**

A naive "deliver mouseUp to whatever is hit" fails the first requirement — release off the
button and the button never hears the `up`, so it stays pressed forever. Rev solves it with
the `drag` flag set at press time. `mouseUp` is delivered to a child when it's **hit *or* a
drag target**:

```cpp
if (containsEvent || isDragTarget) { child->mouseUp(e); }   // reaches off-target presses
if (containsEvent && isPressTarget) { child->click(e); }    // click needs release-OVER
```

So the element that was pressed *always* receives `mouseUp` (because `drag` is still set),
which clears `press` and `drag` — requirement one. But `click` only fires when the element
is **both** the press target **and** currently hit — requirement two, the
cancel-by-sliding-off behavior, for free.

> **`mouseUp` clears `press` before your listener runs**, exactly like `mouseDown` sets it
> first. So inside an `onMouseUp` handler, the button already reads as released. This is why,
> in the jog pad, "recompute intent from every button's `press` flag" gives the right answer
> whether called from a down or an up.

### Drag

While a button is held and the mouse moves, the window also dispatches `mouseDrag` (it
checks the root's `drag` flag in `onCursorPos`). `mouseDrag` propagates only to children
that are themselves `drag` targets — i.e. down the chain of the element where the press
began — so a drag "sticks" to its originating element even as the cursor roams far away
from it. That's what you want for sliders, knobs, and window-resize grips: the gesture
belongs to where it started, not to whatever happens to be under the pointer mid-drag.

### Double-click

A `Button` (the `Event::Button`, not the widget) tracks its own press history, so
double-click is a property of the button state, not a separate event:
`e.mouse.lb.isDoubleClick()` is true when the last two presses were close enough in **time
and position** (defaults 200 ms / 10 px). Cross-platform, no OS round-trip. Text selection
uses exactly this (`Text.ixx`).

### Capture lost

One platform wrinkle, handled for you: if the OS yanks mouse capture mid-drag (alt-tab, a
system dialog steals focus), the gesture would otherwise leave `press`/`drag` stuck on.
The window catches `WM_CAPTURECHANGED` and **synthesizes a release** (`onCaptureLost`) so
the flags unwind cleanly. You never see it; it just means your drag state can't get
wedged.

---

## Focus — and why it's a *chain*, not a point

Focus answers "where do keystrokes go?" In a flat world you'd store one focused element. Rev
stores it as a **chain of `focus` flags down the tree**, and that choice is what makes
nesting work.

Focus is established as a side effect of pressing. During `mouseDown` propagation, a parent
does:

```cpp
if ( containsEvent && !isFocusTarget) { child->gainFocus(e); }  // moved focus in
if (!containsEvent &&  isFocusTarget) { child->loseFocus(e); }  // moved focus out
```

`gainFocus` sets `focus = true` and then recurses into its *hit* children, and so on down
to the leaf. The result: when you click a button buried three layers deep, **every element
on the path** — panel, row, button — gets `focus = true`, not just the button.

Why do that? Because keystrokes propagate down the focus chain (next section), and a
container often wants to handle keys *on behalf of its children*. The jog pad is the
poster child: you click a jog key, which focuses the key — but the *section* is on the
focus chain too, so the section's `keyDown` fires and it can interpret the arrow keys for
the whole pad. If focus were a single leaf pointer, the section would be deaf.

`loseFocus` is the mirror image: it clears `focus` and recurses into focused children that
*no longer* contain the event, unwinding the old chain. Note the ordering — `loseFocus`
sets its own `focus = false` **before** calling your listener — so an `onLoseFocus` handler
reads itself as already unfocused. (Again handy: the jog pad's `onLoseFocus` recomputes its
intent, and because `focus` is already false, the keyboard contribution correctly drops to
zero.)

> **`tabStop`** marks an element as *intended* to be reachable by keyboard focus. It's the
> opt-in flag for tab traversal; today focus is driven primarily by pointer presses as
> described above. Set it on things that should be focusable, but don't rely on it to be
> the thing that grants focus right now — a press on (or within) the element is what puts it
> on the focus chain.

---

## Keyboard — held state, routed down the focus chain

Two things to hold in your head at once:

**1. The `Event` carries key *state*, not key *events*.** When a key changes, the window
updates a sticky `Button` for it — `event.keyboard.arrows.left`, `.ctrl`, `.shift`,
`.enter`, … — *then* dispatches. So inside a handler you ask "is Left held *right now*?"
not "was Left the key that just moved?":

```cpp
if (e.keyboard.arrows.left) { ... }   // currently down
```

This is why holding Left **and** Right at once is naturally expressible — both buttons read
true, and your logic can let them cancel. An event-as-message model would force you to
reconstruct that combined state yourself.

**2. Keystrokes travel down the focus chain.** `keyDown` fires the element's own listeners,
then recurses only into children whose `focus` is set:

```cpp
virtual void keyDown(Event& e) {
    dispatcher->tell(&Element::keyDown, e);
    if (!e.propagate) { return; }
    for (Element* child : std::views::reverse(children)) {
        if (child->targetFlags.focus) { child->keyDown(e); }
    }
}
```

Because focus is a *chain* (previous section), the event visits each ancestor of the
focused leaf, top-down, before reaching the leaf. A container gets the first look and can
`e.propagate = false` to swallow a key its child shouldn't see, or let it through. `keyUp`
is identical with the released-state semantics.

`event.keyboard.key` is a human-readable name of the key in play (`"left"`, `"a"`,
`"enter"`) for when you want to switch on identity rather than poll a specific `Button`.

---

## Text input is a separate channel — on purpose

There's `keyDown`, and there's `textInput`. They are not the same event and must not be
conflated:

- **`keyDown`/`keyUp`** are about *keys as state* — arrows, modifiers, shortcuts, "is Shift
  held." Great for navigation and commands. Terrible for typing text.
- **`textInput`** delivers *committed characters* — `event.keyboard.input` holds the actual
  text (`"a"`, `"é"`, a pasted glyph, an IME composition result). It's what a `TextInput`
  widget consumes.

Why split them? Because the journey from "physical keys" to "the character the user meant"
is genuinely hard — dead keys, IME composition, Shift/AltGr, keyboard layouts. The OS does
that work and emits a *character* event separately from the *key* events. A `keyDown` for
the `'` key might produce no character yet (it's composing `é`); the character arrives later
via `onCharacter` → `textInput`. So: poll `keyboard` `Button`s for held keys and shortcuts;
read `keyboard.input` in `textInput` for what to actually insert. Never reconstruct typed
text by watching `keyDown` — you'll get the hard cases wrong.

`textInput` routes down the focus chain just like keys.

---

## Mouse wheel — innermost-scrollable wins

Scrolling has its own little rule because nested scroll areas are common. `mouseWheel`
offers the scroll to **children first** (the hit ones), and a child consumes it by setting
`e.propagate = false` once it actually scrolls. Only if no child takes it does the current
element try to scroll itself, according to its `scroll` style (`Horizontal`/`Vertical`/
`Both`). Consuming a scroll marks layout dirty (the child origin shifted) and refreshes.

The effect: the **innermost scrollable element under the cursor** eats the wheel, and a
parent list only takes over once the inner one hits its end. Exactly the behavior you
expect from nested scroll panes, and it's just "children first, stop when consumed."

---

## Override vs. listen — two ways in, one rule

You participate in events two ways:

**Register a listener** (the common case) — `onClick`, `onMouseDown`, `onKeyDown`,
`onGainFocus`, `onMouseWheel`, … attach a callback fired when that event reaches the
element. Multiple listeners can stack on one channel; all run. This is wiring from the
*outside* ("when this button is clicked, do X"):

```cpp
button->onClick([this](Event& e) { doThing(); });
```

**Override the virtual** (for subclasses) — redefine `mouseDown`, `keyDown`, `contains`,
etc. to change *behavior and propagation*. This is building a widget from the *inside*.

The one rule that bites people: **the virtuals do the propagation and flag bookkeeping**
(setting `press`, deriving hover, recursing to children). So if you override one and still
want the defaults, **call the base** — usually first:

```cpp
void keyDown(Event& e) override {
    Box::keyDown(e);     // keep default propagation + listeners
    if (e.keyboard.arrows.left) { ... }
}
```

Calling the base *first* gives you bubble-like ordering (children/own-listeners handled,
then your code). Calling it *last* keeps the default top-down feel (your code, then
propagate). Omit it only when you deliberately mean to halt the default machinery.

---

## A worked example: the jog pad

`Gui.Machine.Jog.ixx` leans on almost every rule above, so it's a good capstone.

It wants a unified "jog intent" — which directions the user is currently asking for — fed by
**both** mouse-held keys and the arrow keys, and reflected by highlighting the active keys.
The naive approach (track `onMouseDown`/`onMouseLeave` by hand) fights the cancel-on-leave
semantics. Instead it leans on the flags:

- **Mouse-held state is just `button->targetFlags.press`.** No manual tracking. Press
  capture means a release anywhere clears it, and sliding off a key while holding does
  *not* — precisely the "still held" semantics jogging needs. Each jog key's
  `onMouseDown`/`onMouseUp` simply call one `update(e)` that re-reads every key's `press`.
- **Arrows work because focus is a chain.** Clicking a jog key focuses the key, but the
  section is on the focus chain, so the section's `onKeyDown`/`onKeyUp` fire and fold the
  live arrow state into the same intent.
- **`onLoseFocus` zeroes the keyboard part for free**, because `focus` is already false by
  the time it runs, so the section's focus-gated keyboard read contributes nothing.
- The combined intent is **reflected** (not pushed): each frame's recompute diffs against
  the last and only toggles the highlight style on keys whose state actually changed.

The lesson: don't re-implement what the flags already encode. "Is it held?" is `press`.
"Should this container hear keys?" is being on the `focus` chain. "Did the cursor leave?"
is the `hit`→`hover` transition. Reach for those first.

---

## How not to use the event system

- **Don't store the `Event&`** past your handler — it's reused and reset every dispatch
  (see `Element.Event.About.md`).
- **Don't write `targetFlags`** — read them. They're computed for you each frame. The lone
  exception is `disabled`, via `setDisabled`.
- **Don't reconstruct typed text from `keyDown`.** Use `textInput`/`keyboard.input`.
- **Don't poll `keyboard.key` to detect a held key** — use the `Button` fields; `key` names
  the key currently in play, it isn't held-state.
- **Don't forget the base call** in an overridden virtual when you still want default
  propagation, hit-testing, or flag bookkeeping.
- **Don't expect a single "focused element."** Focus is a chain; a container can be focused
  *and* its child focused at once. That's the design, not a leak.
- **Don't fight click-cancel.** If you want "fires even if released off the element," that's
  `mouseUp`, not `click`. `click` is release-over by definition.
```
