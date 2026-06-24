# How To Really Use Rev

This is the working disposition — the handful of habits that make Rev feel like the easy
framework it is. None of this contradicts `Element.About.md` / `Rev.Element.HowTo.md`; it
sharpens the part agents most often get wrong, which is *how much work to do*.

The recurring failure mode is **over-engineering**: building observer graphs, rebuilding
subtrees, gating everything defensively. Rev rewards doing *less*.

---

## 1. Declare structure once. Reflect state continuously.

Build your children and primitives **in the constructor, once**. After that you only
*adjust* them. Rebuilding the subtree every frame throws away the entire point of a
retained tree and churns layout.

```cpp
struct ToolRow : public Box {
    Text* label = nullptr;
    Button* remove = nullptr;

    ToolRow(Element* parent) : Box(parent, { &Row }, "ToolRow") {
        label  = new Text(this, "", { &Label });   // built ONCE
        remove = new Button(this, { .label = "x" });
    }

    void computeChildren(Event& e) override {       // reflected EACH frame
        label->set(currentToolName());              // adjust, don't recreate
        Box::computeChildren(e);
    }
};
```

- ✅ Keep typed pointers to children you'll talk to later.
- ❌ Don't `new` your children inside `computeChildren`/`compute*`. Adjust the existing ones.

---

## 2. The GUI may mutate app state directly. Then `refresh(e)`.

You do **not** need an observer layer between the GUI and your model for user-driven
changes. The handler is already on the main thread and already holds the `Event&`. So:
**touch the state, then call `this->refresh(e)`.** That one call makes the whole subtree
reconsider, on the next frame, its:

- **children** (`computeChildren`),
- **styles** (`computeStyle`),
- **data/primitives** (`computePrimitives`).

```cpp
deleteButton->onClick([this](Event& e) {
    appState->removeTool(toolName);   // mutate the single owner directly — fine
    this->refresh(e);                 // the branch catches up next frame
});
```

This is the default path for everything interactive. "Reflect, don't push" does **not**
mean "never touch the model" — it means **one owner for the truth**. The GUI editing that
one owner and asking the tree to catch up is correct; `appState` is still the sole owner,
the GUI just edited it.

- ✅ Mutate the owner, then `refresh(e)`.
- ✅ `refresh` *reconsiders*, it doesn't *rebuild* — `computeChildren` still adjusts the
  existing child set, so "declare once" stays intact.
- ❌ Don't build an `Observable`/subscription graph for a plain button-click → state-change.
  That's ceremony the `refresh` already covers.
- ❌ Don't write to `rect` or `resolved`. They are per-frame *outputs*; you read them.

---

## 3. Events are the EASY part. Read flags; attach `on*`.

All the genuinely hard event work — hit-testing, the ancestor hit-chain, reverse
draw-order priority, deriving enter/leave, the press/release/click capture knot,
focus-as-a-chain — is **already done in the base virtuals**. As a consumer your job is two
moves:

1. **Attach a callback** to a child: `child->onClick([this](Event& e){ ... })`.
2. **Read a flag** to reflect input state: `targetFlags.press`, `.hover`, `.focus`,
   `.hit`, `.drag`, `.disabled`.

Do **not** re-implement what the flags already encode. The canonical lesson (the jog pad):

- "Is it held?" → `targetFlags.press` (press-capture means release-anywhere clears it, and
  sliding off while held does *not* — exactly the semantics you want).
- "Should this container hear keys?" → being on the `focus` chain (focus is a *chain*, not
  a single leaf — a container and its child are focused at once, by design).
- "Did the cursor leave?" → the `hit`→`hover` transition (Rev derives `mouseEnter`/
  `mouseLeave` for you; the OS never sends them).

```cpp
// Highlight while pressed — no manual state machine, the flag already knows.
void computeStyle(Event& e) override {
    if (targetFlags.press) styles.add(&Active);
    else                   styles.remove(&Active);
    Box::computeStyle(e);
}
```

- ✅ Override the event *virtual* for your element's intrinsic behavior; use `child->on*`
  to compose children.
- ✅ Call the base in an overridden virtual (it drives propagation + flag bookkeeping) —
  base **last** = your code then children (default top-down); base **first** = children
  then your code (bottom-up).
- ❌ Don't track hover/press/focus by hand with `onMouseDown`/`onMouseLeave` bookkeeping —
  you'll fight click-cancel and get the corner cases wrong.
- ❌ Don't reconstruct typed text from `keyDown`. Use `textInput`/`keyboard.input`.
- ❌ Don't store the `Event&` past your handler — it's reused and reset every dispatch.

---

## 4. `Observable` / dirty-gating is a SCALPEL, not a wall.

`refresh` is the default. Dirty flags and `Observable::changed()` are **performance
pruning** you add *only* at a spot you've identified as expensive — and you add them to
*skip* work, not to *route* it.

```cpp
void computeStyle(Event& e) override {
    if (value.changed()) {            // gate a KNOWN-costly block, not everything
        rebuildExpensiveThing();
        value.changedFlag = false;
    }
    Element::computeStyle(e);
}
```

The textbook case is **animation**. An animating element requests a refresh *every frame
by design* (that's how it keeps drawing), so its `compute*` runs every frame whether or not
the underlying data moved. If that `compute*` does real work — re-deriving a layout,
rebuilding a vertex list, re-measuring text — it becomes a per-frame tax that scales with
everything on screen. The gate separates **redraw** (cheap, every frame) from **recompute**
(expensive, only when data actually changed).

`Observable`'s *other* legitimate use is when the change originates **somewhere you are
not** — telemetry arriving on an animator tick, a load finishing — i.e. there's no handler
holding an `Event&` to call `refresh` from. Then you subscribe, set a dirty flag, and bump
a refresh on the main thread:

```cpp
machine.info.onUpdate(this, [this]{ dirty = true; bump(); });   // off-band change
~Section() { machine.unsubscribe(this); }                        // always unsubscribe
```

- ✅ Build with `refresh`; profile in your head; add a gate when you put something on a
  per-frame loop or catch a `compute*` doing work the data didn't justify.
- ✅ Use `Observable` for out-of-band / cross-thread changes that have no `Event&` to hand.
- ❌ Don't gate defensively everywhere. Premature gating is the same over-engineering, one
  level down.
- ❌ Don't forget to `unsubscribe` an owner in its destructor.

---

## 5. Dynamic children — reconcile, don't rebuild

"Declare once" still holds when the *set* of children is itself variable — a dropdown's
options, a list, a tree, rows that mirror a data vector. You do **not** clear the container
and rebuild it each frame. You **reconcile**: grow/shrink a tracked child vector to match
the data length, then reflect the data into every surviving row. This is the one place the
child set legitimately changes at runtime, and it has a specific shape. It is *not wrapped*
in a helper/function/class yet — but it's short and mechanical, so implement it by hand
following this pattern (see `Rev.Element.Dropdown`'s `computeChildren` for the reference).

Keep a typed vector of the dynamic children as a member, parallel to the data. In
`computeChildren`, do four steps **in order**:

```cpp
std::vector<Text*> options;             // member: the dynamic children, parallel to data

void computeChildren(Event& e) override {

    size_t oldSize = options.size();
    size_t newSize = params.options.size();   // the data driving the set

    // 1. DELETE the surplus (data shrank). delete detaches from the parent
    //    (Element's destructor calls removeChild), so this is the correct removal.
    for (size_t i = newSize; i < oldSize; i++) { delete options[i]; }

    // 2. RESIZE the tracking vector to match the data.
    options.resize(newSize);

    // 3. CREATE the missing (data grew). Wire each row to its INDEX, not the item.
    for (size_t i = oldSize; i < newSize; i++) {
        options[i] = new Text(optionsContainer, "", { &Option });
        options[i]->onClick([this, i](Event& ev) {
            if (i >= params.options.size()) { return; }   // guard: data may have shrunk
            select(params.options[i], &ev);               // resolve at event time
        });
    }

    // 4. REFLECT data into ALL rows (this is the steady-state work).
    for (size_t i = 0; i < newSize; i++) {
        options[i]->content = params.options[i].name;
        options[i]->setDisabled(params.options[i].disabled);
    }

    Element::computeChildren(e);
}
```

Why this shape:

- **It's a delta, so it's already cheap.** When the size is unchanged (the common case),
  steps 1 and 3 do nothing and you only run step 4 — content updates on existing elements.
  No allocation, no layout churn, no gate needed. Only an actual add/remove allocates.
- **Reconciling preserves identity.** The surviving rows are the *same* elements frame to
  frame, so their focus, hover, scroll position, and in-flight transitions survive. A
  clear-and-rebuild drops all of that every frame and reallocates the whole list.

The one gotcha that *will* bite you — **capture the index, not the item:**

- ✅ **Capture `i` and resolve `params.options[i]` at event time** (guard `i` against the
  current size first). *Why:* rows are **reused** across reconciles — step 4 refreshes an
  existing row's content rather than recreating it and its handler. A handler that captured
  the `Item` by value goes stale the moment the list is reordered or an entry is prepended,
  and the row then fires the *wrong* entry (e.g. a newly-inserted "None" row running the old
  first item's action). The index always points at whatever that row currently shows.
- ❌ **Don't `delete` all children and re-`new` them each frame.** *Why:* churns layout,
  drops focus/hover/scroll/transitions, and reallocates every frame for no benefit.
- ❌ **Don't capture the data object by value (or by a pointer that can dangle) in a row's
  handler.** *Why:* reused rows + a by-value capture = stale, wrong-row actions.

> Note for whoever wraps this later: this is begging to become a `reconcile(container,
> vector, data, makeFn, updateFn)` helper. Until it exists, hand-roll the four steps above;
> they are the whole pattern.

## 6. Where the real thinking actually is

It is **not** in the per-element events or layout — those are gentle. The judgment lives
in the **reflect-don't-push discipline once real data flows**: keep the truth in exactly
one owner (the model, or the device object like `Air`), let the GUI mirror it, and after
any change make the tree catch up with `refresh`. That's a discipline question, not a
difficulty one — but it's where a sloppy version rots. Everything else, trust the
framework and do less.
