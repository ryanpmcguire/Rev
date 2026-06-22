# Rev.Element — API

`Rev::Element::Element` is the base node of the UI tree. Every widget (`Box`, `Text`,
`Button`, the 3D view, …) derives from it. It owns: its place in the tree, its style
set, its resolved geometry (`rect`), an event dispatcher, and the per-frame
dirty/refresh bookkeeping.

This documents the API you call and the hooks you override. The layout engine
(`resolveMinima`/`resolveMaxima`/`resolveLayout`/`resolveDims`/`growHorizontalMode`/
`growVerticalMode`/`resolveRects`/`computeLayout`) is internal and driven by the
window each frame — do not call it or depend on its intermediate state. For the
concepts behind dirtiness, resolution, and the frame pipeline, see `Element.About.md`.

---

## Lifecycle & ownership

### `Element(Element* parent = nullptr, StyleList styles = {}, std::string name = "")`
Constructs an element and, if `parent` is given, **adds itself to that parent**. The
parent takes ownership.

- Use: `new Box(parent, { &SomeStyle }, "Name");` — you do not keep or free the result;
  the parent's subtree owns it.
- `name` is for debugging/identification only.
- Don't: `delete` a child you attached to a parent — the parent's destructor frees its
  whole subtree. Deleting it yourself (or twice) is a double-free.

### `virtual ~Element()`
Destroys the element **and recursively all its children**, removes itself from its
parent, and unregisters from shared dirty/focus lists.

- Use: `delete someSubtreeRoot;` to remove a branch of the UI.
- Don't: hold pointers to an element (or any descendant) after deleting an ancestor —
  they're dangling. Destroying a focused text input clears the shared focus pointer for
  you, but other stored pointers are your responsibility.

---

## Building & mutating the tree

### `void addChild(Element* child)`
Attaches `child` (sets its `parent`/`shared`, marks layout dirty, refreshes it).
Usually you don't call this directly — the `Element` constructor does when you pass a
parent.

### `void removeChild(Element* child)`
Detaches `child` from `children` (does **not** delete it). Marks layout dirty.

- Don't: use this to free memory — it only unlinks. To destroy, `delete` the child
  (its destructor calls `removeChild` on its parent).

### `void moveChild(Element* child, Element* target, bool before)`
Reorders an existing child to just before/after `target` (both must be current
children). Reordering changes layout and draw order.

---

## Styling & redraw

Each element has a `StyleList styles` (shared style objects, applied in order) and a
`StylePtr style` (a private per-element override layer). Styles are **re-resolved every
frame** into `resolved.style`; never read raw `Style` values expecting persistence —
read `resolved` if you must, and treat `rect`/`resolved` as read-only outputs.

### `void refresh(Event& e)`
Marks this element dirty for redraw and propagates the request up the tree. Call it
when your data changed and the element must repaint.

- Use: from a `computeChildren`/data handler after mutating what you display.
- Note: most built-in mutators (`setDisabled`, style changes, `addChild`) already
  refresh. You rarely call it directly unless you changed your own backing data.

### `void setDisabled(bool d)`
Sets this element's disabled **intent**. The effective state cascades to the whole
subtree, and `applies.disabled` styles re-resolve. It does **not** itself block input —
each control decides what "disabled" means (Button swallows clicks, etc.).

### `void transition(float* val, float newVal, int ms)`
Animates a single float from its current value to `newVal` over `ms`, easing each
frame. For ad-hoc animation of a value the style system doesn't cover.

- Don't: point it at a value that is overwritten every frame by layout/style — the
  transition will fight the recompute.

---

## Event handling

Two ways to participate in events:

### 1. Register a callback (most common)
`on*` methods attach a listener fired when the matching event reaches this element:

`onClick`, `onMouseDown`, `onMouseUp`, `onMouseMove`, `onDrag`, `onMouseEnter`,
`onMouseLeave`, `onMouseWheel`, `onGainFocus`, `onLoseFocus`, `onKeyDown`, `onKeyUp`,
`onTextInput`, `onRefresh`.

```cpp
button->onClick([this](Event& e) { doThing(); });
```

- Use: for behavior wiring from the element's owner.
- Note: multiple listeners may be attached to one channel; all are called.

### 2. Override the virtual (for subclasses)
The matching `virtual void click/mouseDown/mouseUp/mouseMove/mouseEnter/mouseLeave/
mouseDrag/mouseWheel/gainFocus/loseFocus/keyDown/keyUp/textInput(Event&)` can be
overridden in a subclass to change default behavior and propagation.

- **Call the base** (`Box::keyDown(e);`) at the top of your override unless you
  deliberately intend to stop default handling/propagation — these virtuals also drive
  hit-testing and propagation to children.

### `virtual bool contains(Pos& pos)`
Hit-test: returns whether `pos` is inside this element. Default uses `rect`. Override
for non-rectangular hit shapes.

### `bool interceptHits`
When true and the cursor is over this element, elements drawn behind it don't receive
`hit` this frame (an input shield).

### `bool tabStop`
Marks the element as reachable by keyboard tab focus.

### `TargetFlags targetFlags`
Per-frame input state (`hit`, `hover`, `press`, `focus`, `drag`, `disabled`). Read it to
reflect input state (e.g. in a `compute*` override); treat it as an input, not something
you set (except via `setDisabled`).

---

## Hooks for subclasses (the per-frame pipeline)

Override these to define a widget; the window calls them at the right time. Do not call
them yourself.

- `virtual void computeChildren(Event&)` — rebuild/adjust child elements to reflect data.
- `virtual void computeStyle(Event&)` — adjust styles to reflect data/state.
- `virtual void computePrimitives(Event&)` — adjust draw primitives to reflect style/data.
- `virtual void stencil(Event&)` — emit the clip/stencil shape (kept separate from draw).
- `virtual void draw(Event&)` — paint. If you override, call the base or replicate its
  dirty/transition bookkeeping.
- `virtual void resolveStyle(Event&)` / `virtual void animate(Event&)` — style resolution
  and transition stepping; override only with care.

Declaring structure **once** in the constructor and only *reflecting* live state in
`computeChildren`/`computeStyle` (rather than rebuilding the subtree each frame) is the
intended pattern.

---

## How not to use Element

- Don't free children manually, or keep pointers across an ancestor's deletion.
- Don't call the layout methods or `draw`/`stencil` directly — they're frame-driven.
- Don't write to `rect` or `resolved`; they're recomputed every frame.
- Don't rebuild your whole subtree every frame; build once, reflect state.
- Don't forget the base call in overridden event virtuals when you still want default
  propagation/hit-testing.
- Don't read `rect` before the first layout pass has run.
