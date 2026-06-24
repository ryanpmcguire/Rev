# The Escape Staircase

When Rev seems to be in your way, you do not fight the layer you're on and you do not look
for an exit *from* Rev. You step **down one rung**. Every rung is still Rev, and the rung
below never depends on the one you left (the layering is strict and one-directional). This
is the concrete procedure behind "use less of Rev, within Rev."

Pick the **highest** rung that satisfies the requirement. Descend only as far as you must.

---

## Rung 1 — Style the Box

Most "I need it to look different" is a style change. Set fields on a shared `Style`.

```cpp
static inline Style Panel = {
    .size       = { .width = 280_px, .height = 100_pct },
    .background = { .color = Theme::panelSurface },
    .border     = { .right = { .color = Theme::panelBorder, .width = 1_px } }
};
```

If you can express it in `Style`, stop here. Don't reach lower.

---

## Rung 2 — Per-instance override + reflect

Need a value the shared style can't hold because it's *computed per element*? Use the
private override layer (`style->`) and set it in a `compute*` hook.

```cpp
void computeStyle(Event& e) override {
    thumb->style->position.left = Pct(100.0f * fraction());   // per-instance, computed
    Element::computeStyle(e);
}
```

Shared `Style` objects for anything reused; `style->` for a value you compute. Still no new
types.

---

## Rung 3 — Override the hooks / event virtuals

Need *behavior* the base doesn't give, or a self-measured size? Override the relevant
virtual and call the base (placement of the base call controls ordering — see
How-To §3).

```cpp
void click(Event& e) override {
    if (disabled) { e.propagate = false; return; }   // we decide what disabled means
    Box::click(e);
}

void computeLayout() override {                       // self-measured content
    layout = Layout();
    this->maxWidth = resolved.max.innerWidth;
    measureMyContent();
    layout.size.w = { .val = width, .min = width };
}
```

You haven't left the element tree; you've supplied the one thing it couldn't infer.

---

## Rung 4 — Your own primitive (your own shaders, your own buffers)

The style system can't express the look at all (a custom gauge, a waveform, a node graph,
a specific bevel). Write a `Primitive`. It is a **first-class citizen** — drawn through the
same `Canvas`, clipped by the same stencil stack, composited with its siblings.

Shape (distilled from `Rectangle`): **shared program per type** (guarded by
`Rev.Core.Shared`), **per-instance data** in a mapped buffer.

```cpp
struct Gauge : public Primitive {
    inline static Shared shared;
    inline static Pipeline* pipeline = nullptr;     // ONE program for all gauges

    struct Data { Core::Rect rect; Core::Color color; float value; };
    UniformBuffer* databuff = nullptr;
    Data* data = nullptr;                            // mapped: writing IS the upload

    Gauge(Canvas* c) : Primitive(c) {
        shared.create([this]{ pipeline = new Pipeline(canvas->context, {
            .attribs    = Vertex::attribs,
            .openGlVert = File("./Shaders/Gauge.vert"),
            .openGlFrag = File("./Shaders/Gauge.frag"),
        }); });
        databuff = new UniformBuffer(c->context, sizeof(Data));
        data = static_cast<Data*>(databuff->data);
    }
    ~Gauge() { shared.destroy([this]{ delete pipeline; }); delete databuff; }

    void draw() override {
        pipeline->bind(); databuff->bind(1);
        canvas->drawArraysInstanced(Pipeline::Topology::TriangleFan, 0, 6, 1);
    }
};
```

Hang it on an element via `computePrimitives` (reflect resolved state into `data`) and
`draw` (call yours, then the base). See `Graphics.Primitives.md` for the full field guide.

- ✅ One pipeline per primitive *type*; only genuinely per-instance values in the
  per-instance buffer.
- ❌ Don't make a pipeline per instance. Don't `glBufferSubData` a mapped buffer — write
  through `data`. Don't `#ifdef` a backend in by hand.

---

## Rung 5 — Replace the shaders every Box draws through

You want the change **app-wide** — e.g. to make *every* surface render with a classic
Macintosh hard-bevel border. The `Rectangle` pipeline is **shared across all instances**
(one program per type), so it is a single seam: rewrite `Rectangle.vert`/`.frag` and every
`Box` in the program renders through your shader at once. Layout still solves, events still
flow, the stencil still clips — you changed the *paint* and only the paint.

Two niceties:

- **Variants without forking:** put the token `DEFINITIONS` in the shader and pass
  `.definitions = "#define STENCIL"` for a variant pipeline. One source, two programs
  (this is how the color pass and the stencil-mask pass already coexist).
- **Live iteration:** in debug, `File("./Shaders/...")` is a runtime disk read that
  re-reads on mtime change. Editing the frag updates the look on the **next frame** — no
  transpile, no link, no relaunch. Dial in the bevel by watching it change.

The one honest tax: a shader is authored **per backend** (`.vert`/`.frag` for GL, `.metal`
for Metal) — different languages, not boilerplate.

---

## Rung 6 — Use none of it

The bottom of the staircase is not a trapdoor into someone else's runtime. It's `int
main()`. The executable is a **console** program; the window, graphics, and element tree
are all opt-in. Want to drive a device with sockets/serial and no UI? Import just those
modules. Want nothing? You have a normal C++ program. Every point on the gradient — from
the full retained UI with a 3D viewport down to "print a line and exit" — is a legal place
to stand.

---

## The rule

> Highest rung that works. Descend one rung at a time. Every rung is still Rev, and you
> never have to leave Rev to reach any of them. If you find yourself wanting to escape
> *out* of Rev, you've missed the rung directly below you.
