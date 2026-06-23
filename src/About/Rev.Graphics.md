# Rev.Graphics

The bottom of the stack: the part that actually talks to the GPU. Everything above —
elements, styles, layout, events — eventually has to become triangles on a surface, and
this is the layer that makes that happen. It's small, it's modern, and it has one big
idea that's easy to miss: **Rev does not render *on top of* a graphics API. It owns its
own renderer, and the API underneath is swappable.**

If you've only ever drawn UI through a browser or a toolkit that hands you a 2D
`Canvas`/`Painter`, this layer will feel lower than you're used to — there are vertex
buffers and shaders down here. That's the point. The same machinery that fills a button's
background fills a 3D toolpath mesh, because they're the same kind of thing all the way
down. Read `Graphics.Primitives.md` next for how to *build* on it; this file is the map.

---

## The shape of the layer

From the pixels up:

```
Element  (Box, Text, View3D, …)        "I have a rect and a style"
  └─ Primitive  (Rectangle, Text, Svg, Mesh3d, Lines, …)   "I know how to draw a thing"
       └─ GPU resources  (Pipeline, Shader, VertexBuffer, UniformBuffer, Texture, FrameBuffer)
            └─ Canvas      "the surface + the GPU verbs"
                 └─ backend (OpenGL / Vulkan / Metal)
```

- A **`Canvas`** owns the rendering surface and exposes the verbs: begin/end a frame,
  manage the stencil buffer, issue draw calls. One per window.
- **GPU resource wrappers** (`Pipeline`, `Shader`, `VertexBuffer`, `UniformBuffer`,
  `Texture`, `FrameBuffer`, `CommandBuffer`) are thin, RAII handles around the backend's
  objects. They're the vocabulary a primitive uses.
- A **`Primitive`** is one drawable concept — a rounded rectangle, a run of text, an SVG,
  a 3D mesh. It owns the shaders and buffers it needs and knows how to `compute()` (update
  its data) and `draw()` (issue the calls).
- An **`Element`** doesn't touch the GPU directly. It holds primitive(s) and, each frame,
  *reflects* its resolved style into them — the same "declare once, reflect state" pattern
  as the rest of Rev.

Each layer only knows about the one below it. An element knows primitives; a primitive
knows the canvas and resources; the resources know the backend. Nothing reaches up.

---

## The big idea: the backend is chosen by module identity

Here's the trick that makes the whole thing swappable without a runtime cost. Look at the
module declarations:

- `Graphics/OpenGL/Canvas.OpenGL.ixx` → `export module Rev.Graphics.Canvas;`
- `Graphics/Metal/Canvas.Metal.ixx`  → `export module Rev.Graphics.Canvas;`

**Two files, same module name.** Same for `Shader`, `Pipeline`, `Texture`, and the rest —
each backend provides a complete set of modules under the *same names*. The build
(`clever`) compiles in exactly one backend's set, and everything above just writes:

```cpp
import Rev.Graphics.Canvas;
```

…and transparently gets whichever GPU it was built against. (Vulkan currently sits beside
them under a distinct name, `Rev.CanvasVk` — the work-in-progress backend.)

Why this and not a runtime `virtual Canvas` interface? Because a draw call is hot. A
vtable at the canvas boundary would mean an indirect call per primitive per frame, plus an
abstraction that has to find the lowest common denominator of three APIs. By selecting the
backend at *link time* through the module name, the calls are direct and each backend's
`Canvas` can be exactly as thick or thin as that API wants. **The polymorphism is the
module system, resolved before the program ever runs.** You pay nothing at runtime for the
portability.

> The cost you *do* pay: a primitive's shaders are authored per backend (a `.vert`/`.frag`
> pair for OpenGL/Vulkan, a `.metal` for Metal). That's not boilerplate to abstract away —
> it's the irreducible fact that GLSL and MSL are different languages. The `Pipeline`
> carries all of them in one spec (see below) and the compiled-in backend picks its own.

---

## The Canvas and the frame

`Canvas` (today, the OpenGL one) owns the window's GL context, a `FrameBuffer` it renders
into, a `transform` uniform (the projection matrix), and a `CommandBuffer`. A frame is
bracketed by two calls the window makes for you:

**`beginFrame()`** — makes the context current, and if the window resized, rebuilds the
orthographic projection (`glm::ortho` mapping logical pixels, top-left origin, to clip
space), resizes the render framebuffer, and uploads the new transform. Then it sets up
the per-frame GL state — multisample, alpha blending, the stencil test — and clears
color/depth/stencil. UI is drawn into an **offscreen multisampled framebuffer**, not
straight to the window.

**`endFrame()`** — blits that offscreen framebuffer to the window's default framebuffer
(resolving multisampling) and swaps buffers to present. Rendering to an intermediate target
and blitting at the end is what keeps live-resize clean — newly exposed regions are always
initialized before they're shown.

In between, primitives issue draws through two verbs:

```cpp
canvas->drawArrays(topology, start, count);
canvas->drawArraysInstanced(topology, start, count, instances);
```

`topology` is `TriangleFan` / `TriangleList` / `LineList`. That's the entire draw surface
a primitive needs — bind your pipeline, bind your buffers, call one of these.

---

## Clipping is the stencil buffer

Rev has no "scissor rect stack" abstraction; clipping (`overflow: hide`) is done with the
**stencil buffer**, and the `Canvas` exposes it directly:

```cpp
canvas->stencilPush(depth);   // increment stencil where the test passes
canvas->stencilPop(depth);    // decrement
canvas->stencilSet(depth);    // replace
canvas->stencilDepth(value);  // only draw where stencil <= value
canvas->stencilWrite(bool);   // toggle writing to the stencil buffer
```

The element tree uses this to nest clips: when a `Box` has `overflow: hide`, after drawing
itself it pushes onto a shared **stencil stack** and bumps the stencil depth, so its
descendants are masked to its shape; the depth unwinds as the stack pops. Because it's the
hardware stencil buffer, nesting is free and arbitrarily deep — a clipped panel inside a
clipped panel inside a scroll area just works, no per-level rectangle intersection math.

> The `stencilWrite(enable)` / `colorWrite(enable)` guards short-circuit redundant GL state
> changes (they track the current flag and no-op if unchanged). Small thing, but it's the
> kind of state-thrash that quietly costs you, so it's handled once here.

---

## Resources are *mapped*, not uploaded

This is the detail that explains why the layers above feel so simple. `UniformBuffer` and
`VertexBuffer` allocate **persistent, coherent-mapped** GPU storage:

```cpp
glBufferStorage(..., GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
data = glMapBufferRange(...);   // a CPU pointer that lives for the buffer's lifetime
```

`data` is an ordinary pointer into memory the GPU can see. There is no "upload" step:
**writing through the pointer *is* the upload.** That's why a `Box`, each frame, can just
do:

```cpp
rectangle->data->color = resolved.style.background.color;
```

…and the new color is on the GPU. A primitive maps its instance data struct over a uniform
buffer once, then mutating fields is the whole story. No `glBufferSubData`, no staging, no
dirty-upload bookkeeping. (Coherent mapping means you don't even flush — the driver keeps
it consistent.)

`VertexBuffer` works the same way and additionally describes its **layout** from an
`attribs` list — a vector of float-counts per attribute (e.g. `{2, 4}` = a vec2 then a
vec4, interleaved). It sets up the VAO from that, and a non-zero `divisor` makes the buffer
**per-instance** rather than per-vertex, which is how Rev draws thousands of identical
shapes (every rounded rect is the same 6-vertex fan, instanced with different `Data`).

---

## Pipelines and shaders ship *with* the primitive

A `Pipeline` is a linked shader program plus its vertex layout. Its `Params` is where the
cross-backend authoring lives:

```cpp
new Pipeline(canvas->context, {
    .attribs        = Vertex::attribs,
    .definitions    = "#define STENCIL",          // optional variant switch
    .openGlVert     = File("./Shaders/Rectangle.vert"),
    .openGlFrag     = File("./Shaders/Rectangle.frag"),
    .metalUniversal = File("./Shaders/Rectangle.metal"),
    // .vulkanVert / .vulkanFrag …
});
```

Two things worth understanding:

- **`File("./…")` is an embedded resource, not a runtime file read.** A `Resource` is just
  `{ const unsigned char* data; size_t size; }`. The `clever` build inlines the file's
  bytes *at transpile time*, rewriting `File("./x")` into a static byte literal — so the
  shader source is baked into the executable and the pointer is valid for the whole program
  (`Rev.Core.Resource`). Your primitive's shaders ship *inside the binary*; there's no
  loose `Shaders/` folder to deploy. (The CMake path uses a file atlas to the same effect.)

- **`definitions` gives you shader variants from one source.** The `Shader` loader finds the
  token `DEFINITIONS` in the source and replaces it with your string before compiling. So
  one `Rectangle.frag` becomes both the color pass and the `#define STENCIL` mask pass — no
  duplicate shader files, just a compile-time switch. (Compile or link failure throws with
  the driver's info log, so a bad shader fails loudly at startup, not silently at draw.)

---

## Sharing: one pipeline per primitive *type*, not per instance

A thousand boxes should not compile a thousand copies of the rectangle shader. They don't:
GPU resources that are identical across all instances of a primitive (the pipeline, the
unit vertex buffer) are held in `inline static` fields and guarded by `Rev.Core.Shared`, a
tiny reference counter:

```cpp
shared.create([this]{ /* build pipeline + vertices */ });  // runs only on the first instance
…
shared.destroy([this]{ /* tear them down        */ });     // runs only when the last one dies
```

So the first `Rectangle` ever constructed compiles the program; the rest reuse it; the
program is freed when the final `Rectangle` is destroyed. Per-*instance* data (the uniform
buffer holding *this* box's rect/color/border) is, of course, per instance. This split —
**shared program, per-instance data** — is the standard shape every primitive follows, and
it's why instancing comes naturally: same pipeline, same geometry, N data records.

---

## Why this is the foundation, not a detail

The comparison set stops short of here:

- The **DOM** bottoms out at the browser's compositor — you cannot add a new GPU primitive;
  you can only describe boxes and text and petition the engine through CSS. A `<canvas>` is
  an escape hatch *out* of the model, not a citizen of it.
- **ImGui** deliberately doesn't own a backend — it emits draw lists and makes *you* wire up
  a renderer.
- **Qt** does own a renderer (RHI/scene graph), the closest analog — but adding a custom GPU
  node means `QSGGeometryNode`/`QSGMaterial` ceremony inside a framework built on `moc`.

Rev's bet is that a UI framework should own the descent all the way to the swapchain, and
that "a button" and "a 3D mesh" should be the *same kind of object* — both `Primitive`s
drawn through the same `Canvas`. That's what lets the CAM 3D viewport (`Mesh3d`,
`Lines3d`, ray-cast picking) live in the *same tree* as the toolbar, rather than as a
foreign surface embedded in it. And it's what makes the next document possible: because the
primitive system is open, **anyone can ship a new element that brings its own primitive,
which brings its own shaders and resources** — a self-contained, GPU-accelerated widget,
with nothing special granted to the built-ins.

See `Graphics.Primitives.md` for how to write one.

---

## How not to use Rev.Graphics

- **Don't call `glXxx` from an element.** Elements reflect into primitives; primitives talk
  to the canvas/resources. Reaching past that breaks the backend abstraction and the
  layering.
- **Don't assume the backend.** Code above `Rev.Graphics.*` must not name OpenGL/Metal
  types. If you author a primitive, supply shaders for the backends you target via the
  `Pipeline` params; don't `#ifdef` a backend in by hand.
- **Don't re-upload what's mapped.** A `UniformBuffer`/`VertexBuffer`'s `data` pointer is
  live GPU memory — write through it; there is no separate upload to perform.
- **Don't create GPU resources off the render thread / without a current context.** The
  wrappers assert a current context (`requireContext`); construct and destroy them where the
  canvas is live.
- **Don't make a pipeline per instance.** Share the program across the type (`Rev.Core.Shared`)
  and keep only genuinely per-instance data in per-instance buffers.
- **Don't reach for a "clip rect."** Clipping is the stencil buffer; nest it with the
  stencil depth, don't invent a parallel mechanism.
```
