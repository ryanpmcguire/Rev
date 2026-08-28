# What Rev Really Is

Rev presents as "a desktop UI framework with a 3D stack." That's true, but it's the
surface. Underneath, Rev is one idea applied with unusual discipline:

> **Rev is strictly additive over a plain C++ program.** It starts you at `int main()`
> and lets you *add* exactly the layers you want — window, graphics, element tree, controls,
> 3D — each one a clean increment, each one removable without disturbing the ones below it.

Internalize that and almost everything else about Rev becomes predictable.

---

## 1. The footprint is elective, all the way down to nothing

The built executable is a **console subsystem** program (`/subsystem:console`). The window
is opt-in. The graphics layer is opt-in. The element tree is opt-in. There is no mandatory
core that insists on running.

- Want the full retained UI with a 3D viewport? Import the whole stack.
- Want only sockets/serial to talk to a device, no UI at all? `import Rev.Client;
  import Rev.Serial;` and skip everything above.
- Want only the geometry/color math? `import Rev.Core.*`, nothing else.
- Want none of it? You have a normal C++ console app. That is a legal, coherent place to
  stand.

The subsystems compose **upward** from the CLI; they are not a monolith you opt out of in
pieces. This is why you can never get trapped: the limit case of "use less of Rev" is a
plain program, and no framework can get in your way once it's been reduced to that.

**Consequence for you:** never assume a layer is required. If a task doesn't need the
element tree, don't drag it in. Reach for the *thinnest* slice that does the job.

---

## 2. The escape hatch goes DOWN, not OUT

This is the property that matters most, and the one most frameworks fail.

In a typical framework, the escape hatch is a **border crossing**: a `<canvas>` is you
*leaving* the DOM; an FFI call is you *leaving* the runtime; "drop to native" hands you a
foreign, quarantined surface and you're on your own. The hatch leads *out* of the
framework's world.

In Rev the hatch leads **down**, and the floor below is the same material:

- Hit a wall at the element level? Drop to a **primitive** — still a `Primitive`, still
  drawn through the same `Canvas`, still clipped by the same stencil stack, still
  composited with its siblings.
- Don't want the layout solve for one element? Override `computeLayout` and report your own
  extent. You didn't leave layout; you supplied the one number it couldn't infer.
- Don't want a control's behavior? It's a `struct`. Stop using its hooks, derive from
  `Box` (or bare `Element`) instead.
- Need a look the style system can't express? Write your own primitive with its own
  shaders, or **replace the shaders every `Box` draws through** (the `Rectangle` pipeline
  is shared — one program per primitive *type*, so changing it once changes all of them).

At every level "the way out" is to use a **thinner slice of the same thing**, and the
thinner slice is a first-class citizen — not an embedded foreign object. The 3D viewport
proves it: `Mesh3d` is exactly as native as `Rectangle`, so the viewport is a *sibling* of
the toolbar, not a surface bolted into it.

**Why this works:** the layering is strict and one-directional. `Element` knows
`Primitive`; `Primitive` knows `Canvas`/resources; resources know the backend. **Nothing
reaches up.** So you can stand on any layer and the ones below it don't depend on the one
you walked away from. Descending is always cheap and lateral, never a cliff.

**Consequence for you:** when Rev seems to block you, do not fight the layer you're on and
do not look for an exit from Rev. Look for the rung *below*. See
[The-Escape-Staircase.md](The-Escape-Staircase.md).

---

## 3. The abstraction has no lies in it

Nothing load-bearing is hidden, so nothing hidden can trap you. This is the deeper reason
for "I've never hit a limit that using less of Rev couldn't avoid."

- **No string units.** A length is a typed `Dist` (`14_px`, `50_pct`, `Grow()`), checked
  by the compiler. There is no `"14px"` parser to surprise you.
- **No DOM diffing.** You build the tree and *reflect* state into it; you don't return
  virtual trees to a reconciler that guesses what changed. You say what changed, by calling
  `refresh`.
- **No runtime backend indirection.** The GPU backend is chosen by *module identity* at
  link time (two files, same `export module Rev.Graphics.Canvas;`), not a `virtual Canvas`.
  The portability costs you nothing at runtime.
- **No hidden upload.** A `UniformBuffer`/`VertexBuffer` is persistent-mapped; writing
  through its `data` pointer *is* the upload. `box->rectangle->data->color = ...;` is on the
  GPU. There is no staging step being hidden from you.

Where a real seam exists, Rev shows it rather than papering over it. The clearest example:
a primitive's shaders are authored **per backend** (a `.vert`/`.frag` for GL, a `.metal`
for Metal) because GLSL and MSL are genuinely different languages. Rev refuses to invent a
fake unified shader language, because that abstraction *would be a lie* — a
lowest-common-denominator fiction hiding a real difference. The honest seam is the feature,
not the bug.

**Consequence for you:** trust what the docs say a thing does; there isn't a hidden
mechanism underneath contradicting it. And don't try to "improve" a Rev seam by hiding it
behind a wrapper that pretends a difference away — that reintroduces exactly the kind of
lie Rev is built to avoid.

---

## 4. It doesn't do everything for you — but it never leaves you hanging

Rev deliberately declines the "convenience layer that also stands in your way." You call
`refresh` yourself. You place the performance gate yourself. You write the per-backend
shader yourself. None of those are *ceremony* — each is the actual decision, handed to you
without a wrapper, at the moment it matters.

What keeps that from being a cold start is that the patterns are **shown, not just
permitted**: the framework did the genuinely hard, invariant work (the constraint-correct
layout solve, the event flag machinery, nested stencil clipping, mapped buffers) and then
got out of the way for the part that is actually yours. Read the existing elements (`Box`,
`Text`, `Button`, `Checkbox`, `Slider`) as worked examples; they *are* the documentation of
the idiom.

**The summary:** Rev spends its complexity budget on the hard invariants and leaves yours
for you. Simplicity and control aren't a trade here — they're the same design decision
(refuse the lying middle layer) seen from two ends.
