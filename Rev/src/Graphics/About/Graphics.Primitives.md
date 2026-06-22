# Rev.Primitives — writing your own

`Rev.Graphics.md` is the map of the rendering layer. This is the field guide to its most
important extensibility point: the **`Primitive`**. By the end you should be able to build
a new drawable — with its own shaders, its own GPU buffers, its own draw call — and hang it
off an element, exactly the way `Box` hangs a `Rectangle` off itself.

That sentence is the whole reason this layer is shaped the way it is. **Rev was designed so
that you can make and ship your own elements that carry their own graphics primitives —
which themselves carry their own shaders and resources.** Nothing about the built-in
`Rectangle` or `Text` is privileged. A primitive you write in your own project is a
first-class citizen of the render tree, instanced and clipped and composited like any other.
This document is mostly about *why that's true* and *how to do it right*.

---

## What a primitive actually is

The base is almost nothing (`Rev.Primitive`):

```cpp
struct Primitive {
    Canvas* canvas;
    Primitive(Canvas* canvas) : canvas(canvas) {}
    virtual void compute() {}   // update my data to match the world
    virtual void draw()    {}   // issue my GPU calls
};
```

A `Canvas*` and two verbs. That's deliberately tiny: the contract is "you can update, and
you can draw," and everything else — what buffers you own, what shaders you run, what
geometry you submit — is *yours*. The base imposes no schema, so it can't get in your way.

The split between `compute()` and `draw()` mirrors the element pipeline (`computePrimitives`
→ `draw`) and exists for the same reason: updating data and issuing draws are different
costs at different times. `compute()` is "make my mapped buffers reflect current state";
`draw()` is "bind and submit." Many primitives barely need `compute()` because their data is
written directly by the owning element (more on that below).

---

## The anatomy of a real one

Here's the shape every non-trivial primitive follows, distilled from `Rectangle`. There are
two tiers of state — **shared across all instances of the type**, and **per instance** — and
keeping them straight is the entire craft.

### Tier 1 — shared resources (one per type)

The shader program and the unit geometry are identical for every instance, so they live in
`inline static` fields, built once and reference-counted with `Rev.Core.Shared`:

```cpp
struct MyPrimitive : public Primitive {

    inline static Shared        shared;
    inline static Pipeline*     pipeline = nullptr;
    inline static VertexBuffer* vertices = nullptr;

    void createShared() {
        pipeline = new Pipeline(canvas->context, {
            .attribs        = Vertex::attribs,
            .openGlVert     = File("./Shaders/MyPrimitive.vert"),
            .openGlFrag     = File("./Shaders/MyPrimitive.frag"),
            .metalUniversal = File("./Shaders/MyPrimitive.metal"),
        });
        vertices = new VertexBuffer(canvas->context, { .num = 6, .divisor = 1, .attribs = Vertex::attribs });
    }

    void destroyShared() {
        delete pipeline;
        delete vertices;
    }
```

`Shared` is a one-field reference counter: `create()` runs your lambda **only when the first
instance asks**, `destroy()` runs teardown **only when the last instance leaves** (see
`Rev.Core.Shared`). So your program is compiled once no matter how many of these you make.

### Tier 2 — per-instance data (one per object)

What differs between instances — this object's position, color, parameters — lives in a
uniform buffer mapped over a plain struct:

```cpp
    struct Data {
        Core::Rect rect;
        Core::Color color;
        // … whatever your shader's instance block needs, laid out to match it
    };

    UniformBuffer* databuff = nullptr;
    Data*          data     = nullptr;

    MyPrimitive(Canvas* canvas) : Primitive(canvas) {
        shared.create([this]{ createShared(); });          // first-instance setup
        databuff = new UniformBuffer(canvas->context, sizeof(Data));
        data     = static_cast<Data*>(databuff->data);     // a pointer into GPU-visible memory
        *data    = { /* sane defaults */ };
    }

    ~MyPrimitive() {
        shared.destroy([this]{ destroyShared(); });        // last-instance teardown
        delete databuff;
    }
```

The line that matters: `data` aliases the **persistent-mapped** uniform buffer
(`Rev.Graphics.md` → "Resources are mapped"). Writing `data->color = …` *is* the upload.
There is no `compute()`-time copy unless you want one — the owning element typically writes
these fields straight in `computePrimitives`.

### The draw

`draw()` binds the shared program, the shared geometry, and this instance's data block, then
issues one (instanced) call:

```cpp
    void draw() override {
        pipeline->bind();
        vertices->bind();
        databuff->bind(1);                                          // binding point 1 = instance data
        canvas->drawArraysInstanced(Pipeline::Topology::TriangleFan, 0, 6, 1);
    }
};
```

That's a complete primitive: shared program + per-instance uniform block + one draw. The
`transform` (projection) is already bound at binding point 0 by the canvas each frame, so
your vertex shader has the screen mapping for free.

---

## Shaders: authored per backend, shipped in the binary

Your primitive's shaders are *your* files, and they travel *with* your code:

- Author a `.vert`/`.frag` for the GLSL backends and/or a `.metal` for Metal, and list them
  in the `Pipeline` params. The compiled-in backend takes its own; the others are ignored.
- Reference them with `File("./Shaders/MyPrimitive.vert")`. Under `clever`, the bytes are
  **inlined at transpile time** into a static literal, so the shader is baked into the
  executable — there is no `Shaders/` directory to ship alongside your app
  (`Rev.Core.Resource`). The path is resolved relative to the source file, so it works the
  same from anyone's project.
- Need variants (a fill pass and a mask pass, say)? Put the token `DEFINITIONS` in the shader
  and pass `.definitions = "#define STENCIL"` for the variant pipeline. One source, two
  programs. The `Rectangle` does exactly this for its color vs. stencil pass.

A compile or link error throws at construction with the driver's log — so a broken shader
stops you at startup with a message, it doesn't render garbage.

> **Vertex layout is data, not code.** `attribs` is a list of float-counts per attribute;
> the `VertexBuffer` builds the VAO from it and (with a `divisor`) makes the buffer
> per-instance. Match the `attribs` you give the `Pipeline` and the `VertexBuffer` to the
> `in` variables of your vertex shader and you're done — no manual `glVertexAttribPointer`.

---

## Hanging it on an element

A primitive is inert until an element owns it and feeds it. The contract is three of the
element pipeline hooks (`Element.About.md` → "the per-frame pipeline"):

```cpp
struct MyWidget : public Box {            // or : public Element
    MyPrimitive* prim = nullptr;

    MyWidget(Element* parent) : Box(parent) {
        prim = new MyPrimitive(shared->canvas);   // shared->canvas is the window's canvas
    }
    ~MyWidget() { delete prim; }

    void computePrimitives(Event& e) override {
        // Reflect resolved state into the primitive's mapped data — this is the "upload".
        prim->data->rect  = rect;
        prim->data->color = resolved.style.background.color;
        Box::computePrimitives(e);            // let the base reflect its own primitive too
    }

    void draw(Event& e) override {
        prim->draw();
        Box::draw(e);                         // base draws its rect, handles clipping, recurses
    }
};
```

This is the same **"declare once, reflect state"** discipline as everywhere else in Rev: the
element doesn't rebuild anything per frame, it just writes the current resolved values into
the primitive's data block, and because that block is mapped GPU memory, the change is live.
Note the base calls — `Box::computePrimitives`/`Box::draw` keep the box's own background,
border, stencil clipping, and child recursion working; omit them only if you're replacing
that behavior wholesale.

If your widget needs to **clip its children**, you don't do anything special — set
`overflow: hide` in its style and `Box` pushes your shape onto the stencil stack for you. If
your *primitive itself* should act as a clip mask, give it a `stencil()` method (as
`Rectangle` does) that draws into the stencil buffer with `canvas->stencilWrite(true)`.

---

## A worked example: where this is already load-bearing

The CAM app's 3D viewport isn't a special case bolted onto the UI — it's this exact pattern
at a larger scale. `View3D` is an element; `Mesh3d`, `Lines3d`, and friends are primitives
with their own 3D shaders and their own vertex buffers (`Vertex3` layout); picking is a
`hitTest` ray-cast the element runs on mouse events. A toolpath mesh and a jog button are
*siblings* — both elements holding primitives, both drawn through the same canvas, both
clipped by the same stencil stack. That symmetry is only possible because the primitive
system is open: the 3D primitives use nothing the built-ins have and you don't.

So when you write a waveform view, a node graph, a custom gauge, a shader-based gradient —
you're not finding an escape hatch out of the framework. You're using the framework the way
its own 3D engine does.

---

## How not to write a primitive

- **Don't put the pipeline in per-instance state.** Shaders are shared across the type via
  `Rev.Core.Shared`; only data that genuinely differs per object goes in a per-instance
  buffer. A pipeline-per-instance recompiles your shader N times and leaks the savings of
  instancing.
- **Don't `glBufferSubData` your mapped buffers.** They're persistent-coherent-mapped —
  write through `data`. Re-uploading defeats the design and risks fighting the driver.
- **Don't hardcode a backend.** Supply shaders per backend in the `Pipeline` params; never
  `#ifdef GL` inside a primitive. The backend is chosen by the build, not by your code.
- **Don't construct/destroy GPU resources without a live context.** Build them where the
  canvas is current (construction under the element tree is fine); the wrappers will assert
  otherwise.
- **Don't forget the base hooks.** Overriding `computePrimitives`/`draw`/`stencil` without
  calling `Box::`/`Element::` drops the background, border, clipping, and child recursion.
  Call up unless you mean to replace them.
- **Don't rebuild geometry every frame if it's static.** If the shape is fixed and only its
  parameters change, keep the vertices shared and push the change through the instance data —
  that's the fast path the whole layer is built around.
```
