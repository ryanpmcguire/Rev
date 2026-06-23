# Rev.Element — How To Extend Element (A Guide)

This is the hands-on companion to `Element.About.md` (the concepts) and `Element.Api.md`
(the reference). It walks through *how elements are actually built in this codebase* and,
more importantly, **how you should build yours**. Every pattern below is taken from real
elements — `Box`, `Text`, `Button`, `Checkbox`, `Slider`, `Collapsible` — so when in
doubt, open those files and read alongside.

The golden rule runs through everything here: **declare your structure once, then reflect
live state into it each frame.** You do not rebuild your subtree, and you do not drive the
frame — you fill in hooks the window calls for you.

---

## 1. The skeleton of an element

Every element is a C++20 module that exports a `struct` deriving from `Element` (or from a
richer base like `Box`). The shape is always the same:

```cpp
module;

#include <string>
// ... other std includes

export module Rev.Element.MyThing;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;
import Rev.Element.Box;        // if you extend Box

export namespace Rev::Element {

    // Styles live in their own namespace, declared once, shared by all instances.
    namespace MyThingStyle {
        Style Self = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size   = { .width = 100_pct }
        };
    }

    struct MyThing : public Box {

        // child pointers + state declared here

        MyThing(Element* parent, StyleList styles = {}) : Box(parent, styles, "MyThing") {
            this->styles.prepend(&MyThingStyle::Self);
            // ...build children once...
        }
    };
}
```

See `Collapsible.ixx` and `Slider.ixx` for this exact layout — a style namespace next to
the struct, default styles prepended in the constructor.

### Extend `Box`, not `Element`, when you want to paint

`Element` is the bare node: tree membership, styles, events, layout. It draws *nothing*.
`Box` adds a `Rectangle` primitive — background, border, radius, shadow, and overflow
clipping. If your element's own root should have a background or border, derive from `Box`.
`Collapsible` says exactly why it does this:

> Extends Box (not Element) so the control's root can paint its own background/border…

`Text` and `Button` extend `Box` for the same reason. `Checkbox` and `Slider` extend bare
`Element` because their *root* paints nothing — the visible parts are child `Box`es.

---

## 2. Build the structure once, in the constructor

The constructor is where you create your children and primitives — **once**. After that
you only adjust them. This is the single most important habit; rebuilding children every
frame throws away the entire point of a retained tree.

```cpp
Checkbox(Element* parent, Params p = Params::Default(), StyleList styles = {})
    : Element(parent, styles) {

    this->name = "Checkbox";
    this->styles.prepend(&Control);

    this->params = p;
    this->value  = params.def;

    label = new Text(this, params.label, { &Label });

    checkbox = new Box(this, { &CheckboxBox, &CheckboxFocus });
        check = new Svg(checkbox, File("./check.svg"), { &CheckboxMark });

    checkbox->onClick([this](Event& e) { /* ... */ });
}
```

Notes that generalize:

- **Passing `parent` attaches the child.** `new Text(this, ...)` adds itself to `this`.
  You never keep or `delete` these yourself — the subtree owns them. (Indenting nested
  `new`s, as above, is the house style for showing tree depth.)
- **Keep typed pointers** to children you'll talk to later (`label`, `checkbox`, `check`).
- **Primitives are created in the constructor and freed in the destructor.** `Box` makes
  its `Rectangle` in the ctor and `delete`s it in `~Box`; `Text` does the same for its
  `Text`/`Lines` primitives. If you add a primitive, match that lifecycle.

---

## 3. Styling: prepend your defaults so the caller wins

A `StyleList` is applied in order — later styles override earlier ones. The contract in
this codebase: **a control's own theme styles are the defaults, so they go *first*, and
caller-supplied styles go after.** That means `prepend` your defaults onto the list the
caller handed you:

```cpp
this->styles.prepend(&MyThingStyle::Self);     // default sits ahead of caller styles
```

`Button` spells out the ordering rule (and the reverse-prepend trick to keep a base/hover
pair in the right final order):

```cpp
// prepend in reverse (hover, then base) so final order is [base, hover, ...caller]
this->styles.prepend(&ControlTheme::ButtonPrimaryHover);
this->styles.prepend(&ControlTheme::ButtonPrimary);
```

For per-instance, non-shared tweaks there is the private override layer reached through
`style->` (a `StylePtr`). `Slider` nudges its thumb that way each frame:

```cpp
thumbContainer->style->position.left = Pct(100.0f * pctVal);
```

Use shared `Style` objects (added to `styles`) for anything reused; use `style->` for a
value you compute per element.

---

## 4. Behavior: override the virtual for *your* element, use `on*` for *children*

This is the distinction people get wrong, so it's worth stating plainly:

> **Override the event virtual to define how *your element* behaves.
> Call `child->on*(...)` to react to *a child you own*.**

### Override the virtual for intrinsic behavior

When the behavior *is* what your element does, override the matching virtual. `Button`
defines "a click does nothing when disabled" by overriding `click`:

```cpp
void click(Event& e) override {
    if (disabled) { e.propagate = false; return; }
    Box::click(e);
}
```

`Text` owns text editing, so it overrides `mouseDown`, `mouseDrag`, `keyDown`,
`textInput`, `gainFocus`, `loseFocus` — all the input that *is* the text element.

Why override instead of `this->onClick(...)` on yourself? The virtual is the real entry
point: it drives hit-testing and propagation to children, a subclass can extend it, and
the behavior travels with the type. Wiring a listener to yourself in your own constructor
is a parallel, weaker path — exactly the kind of "second way to do the same thing" the
architecture rules warn against.

### Always call the base (and mind *where* you call it)

The base virtual does the default work: firing listeners and propagating to children. Call
it unless you deliberately want to stop. **Where** you place the call decides ordering,
per the top-down model in `Rev.About.md`:

- **Base call last** → your code runs first, *then* children. This is the default
  top-down direction. `Text::keyDown` does its editing, then `Box::keyDown(e);` at the
  end:

  ```cpp
  void keyDown(Event& e) override {
      // ...handle editing...
      this->refresh(e);
      Box::keyDown(e);          // base last: self before children
  }
  ```

- **Base call first** → children run before your code (bottom-up). `Text::gainFocus` calls
  `Box::gainFocus(e)` early because it wants the focus state settled before it registers
  itself as the focused editable.

Pick deliberately; don't scatter the base call.

### Use `on*` to compose children

When you assemble an element from sub-elements, wire their events with `on*`. `Checkbox`
toggles when *its checkbox child* is clicked; `Slider` reacts to drags on *its track*;
`Collapsible` toggles when *its chevron* is clicked:

```cpp
checkbox->onClick([this](Event& e) {
    if (this->targetFlags.disabled) { return; }
    this->value = !value;
    this->refresh(e);
});

sliderContainer->onDrag([this](Event& e) {
    if (setVal(posToVal(e.mouse.pos))) { refresh(e); }
});

arrow->onClick([this](Event& e) { toggle(e); e.propagate = false; });
```

Capture `this`, mutate your state, and `refresh(e)`. Set `e.propagate = false` when the
event shouldn't travel further (the chevron toggling shouldn't also trigger a header
click).

> A pragmatic exception you'll see: `Button` attaches `onKeyDown` *to itself* to make
> Enter/Space activate it (`onKeyDown([this](Event& e){ click(e); ... })`). That's a
> deliberate convenience binding on top of its overridden `click`, not a replacement for
> it. The rule still holds — the *click behavior* is the override; the key binding just
> routes into it.

---

## 5. Reflecting state: the `compute*` hooks

You never repaint or relayout by hand. You change your backing data, mark what's dirty,
and the per-frame pipeline calls your hooks. Override the hook that matches *what kind* of
change it was (the pipeline order is in `Element.About.md`):

- `computeChildren` — adjust the child set to match data.
- `computeStyle` — push data/state into styles.
- `computePrimitives` — push style/data into draw primitives.

### Reflect into styles with `computeStyle`

`Checkbox` turns its checked state into styles — adding/removing shared styles and setting
the checkmark's opacity — only when the value actually changed:

```cpp
void computeStyle(Event& e) {
    if (value.changed()) {
        if (value) { checkbox->styles.add(&CheckboxChecked); check->opacity = 1.0f; }
        else       { checkbox->styles.remove(&CheckboxChecked); check->opacity = 0.0f; }
        value.changedFlag = false;
    }
    Element::computeStyle(e);   // base call as always
}
```

`Slider` reflects its numeric value into the thumb's position the same way:

```cpp
void computeStyle(Event& e) override {
    float pct = (data.val - data.min) / (data.max - data.min);
    thumbContainer->style->position.left = Pct(100.0f * pct);
    Element::computeStyle(e);
}
```

### Gate work with `Observable` + `.changed()`

Don't recompute every frame — recompute when inputs move. `Observable<T>` records changes;
`.changed()` tells you, and you reset the flag once you've handled it. `Text` uses it to
decide whether a restyle is even needed:

```cpp
void computeStyle(Event& e) override {
    if (content.changed() || editable.changed() || selectable.changed()) {
        this->dirty.style = true;     // force a restyle this frame
    }
    Box::computeStyle(e);
}
```

### Reflect into primitives with `computePrimitives`

This is where you copy `resolved.style` and `rect` into a primitive's data. `Box` is the
canonical example — it reads the resolved background, border, radius, and shadow into its
`Rectangle::Data`, then calls the base:

```cpp
void computePrimitives(Event& e) override {
    Rectangle::Data& data = *rectangle->data;
    data.rect  = this->rect.rounded();
    data.color = resolved.style.background.color;
    // ...borders, corners, shadow...
    Element::computePrimitives(e);   // base last
}
```

Read `resolved` and `rect` here — they are the computed *outputs* for this frame. Never
write them.

---

## 6. Drawing your own pixels: `stencil` and `draw`

If your element paints something the style system doesn't cover, override `draw` (and
`stencil` if you clip). The iron rule: **call the base**, or replicate its dirty/transition
bookkeeping yourself. `Box` draws its rectangle then defers to `Element::draw`:

```cpp
void stencil(Event& e) override { rectangle->stencil(); Element::stencil(e); }

void draw(Event& e) override {
    rectangle->draw();
    // ...overflow stencil handling...
    Element::draw(e);
}
```

`Text` draws its glyphs after the box, and only draws the caret when focused and editable:

```cpp
void draw(Event& e) override {
    Box::draw(e);
    text->draw();
    if (targetFlags.focus && editable) { line->draw(); }
}
```

Note `targetFlags` being *read* to decide what to paint — that's the intended use: read
per-frame input state, don't set it (except `disabled`, via `setDisabled`).

---

## 7. Self-measured content: `computeLayout`

Most elements get their size from their children via the flex pass. If instead your element
computes its own intrinsic size (text is the classic case), override `computeLayout` to
report it. `Text` lays out its glyph lines and publishes the measured extent:

```cpp
void computeLayout() override {
    layout = Layout();
    this->maxWidth = resolved.max.innerWidth;
    this->layoutText();
    layout.size.w = { .val = width, .min = width };
    layout.size.h = { .val = height, .min = height };
}
```

Content-driven minima live outside `resolved.style`, so a change to them slips past the
style-diff gate. When your measured minimum moves, raise layout yourself —
`Text::resolveStyle` does exactly this:

```cpp
if (resolved.minContentWidth != prevMinContentWidth /* ... */) {
    shared->layoutDirty = true;
}
```

---

## 8. Disabled state

`disabled` is intent that cascades to the subtree and re-resolves `*Disabled` styles — but
it does **not** block input on its own; each control decides what disabled *means*.
Override `setDisabled` to record your own intent and chain to the base, then honor it in
your handlers:

```cpp
void setDisabled(bool d) {
    disabled = d;
    Element::setDisabled(d);   // cascades + restyles the subtree
}

void click(Event& e) override {
    if (disabled) { e.propagate = false; return; }  // we choose: a disabled button is inert
    Box::click(e);
}
```

`Checkbox` checks `targetFlags.disabled` in its child's click handler instead — same idea,
different surface.

---

## 9. When to call `refresh`

`refresh(e)` marks you dirty for redraw and propagates upward. Most built-in mutators
(`setDisabled`, style edits, `addChild`) already refresh, so you rarely call it. You *do*
call it after changing your **own backing data** in an event handler — every `on*` example
above ends with `refresh(e)` for that reason. If you only changed a non-geometric style,
that's all you need; if you changed something content-sized, also set `shared->layoutDirty`
(see §7).

---

## 10. Checklist — the house style in one place

- **Derive from `Box`** if your root paints; from `Element` if it doesn't.
- **Build children + primitives once** in the constructor; free primitives in the
  destructor. Never rebuild the subtree per frame.
- **`prepend` your default styles** so caller styles win; use `style->` for per-instance
  computed values.
- **Override the event virtual** for your element's intrinsic behavior; **use `child->on*`**
  to compose children. Always call the base in overrides unless you mean to stop — and
  place the base call to control self-vs-children ordering.
- **Reflect state in `compute*`**, gated by `Observable::changed()`; read `resolved`/`rect`
  /`targetFlags`, never write them.
- **Override `computeLayout`** only for self-measured content, and raise `layoutDirty` when
  content minima move.
- **`refresh(e)`** after mutating your own data.

When unsure, mirror the nearest real element: `Box` for painting, `Text` for input +
self-measurement, `Button`/`Checkbox`/`Slider`/`Collapsible` for composing children and
reflecting state.
