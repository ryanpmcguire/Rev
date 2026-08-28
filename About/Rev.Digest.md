# Agent context — Rev (Digest)

**What this is.** A curated, load-bearing slice of the Rev framework — the docs plus the
source modules you need to reason about almost any task that touches the UI, style, layout,
event, or graphics core. It is the "core parts of Rev," assembled as one importable unit.

**When to load it.** `rev-core.md` is always loaded and is enough for routine work. Pull in
*this* digest when a task needs deeper, cross-cutting understanding of Rev itself — touching
the element pipeline, the layout engine, the event system, the style cascade, or writing a
primitive. For the *entire* framework (every backend, every control, the 3D stack, all
platform layers), load `Agent.Rev.All.md` instead.

**How to read it.** Read the `About/` docs first for the model, then the source. Treat all of
it as known, current truth. Paths are relative to this file (`.claude/rules/`).

---

## Concepts — the About docs (read these first)

@../../Rev/src/About/Rev.About.md
@../../Rev/src/About/Rev.Core.md
@../../Rev/src/About/Rev.Style.md
@../../Rev/src/About/Rev.Graphics.md
@../../Rev/src/Element/About/Element.About.md
@../../Rev/src/Element/About/Element.Api.md
@../../Rev/src/Element/About/Element.Event.About.md
@../../Rev/src/Element/About/Element.Events.md
@../../Rev/src/Element/About/Rev.Element.HowTo.md
@../../Rev/src/Graphics/About/Graphics.Primitives.md

## Essence — what Rev really is, and how to really use it

The disposition behind the model: read these to understand *why* Rev is shaped the way it
is and how to work with the grain (most Rev mistakes are disposition mistakes, not API
mistakes). Start with the README.

@../../Rev/About/Essence/What-Rev-Really-Is.md
@../../Rev/About/Essence/How-To-Really-Use-Rev.md
@../../Rev/About/Essence/The-Escape-Staircase.md
@../../Rev/About/Essence/Dos-And-Donts.md

## Core — geometry, color, reactivity, time

@../../Rev/src/Core/Geometry/Pos.ixx
@../../Rev/src/Core/Geometry/D3/Pos3.ixx
@../../Rev/src/Core/Rect.ixx
@../../Rev/src/Core/Lrtb.ixx
@../../Rev/src/Core/Color.ixx
@../../Rev/src/Core/Vertex.ixx
@../../Rev/src/Core/Dispatcher.ixx
@../../Rev/src/Core/DirtyFlag.ixx
@../../Rev/src/Core/Observable.ixx
@../../Rev/src/Core/Process.ixx
@../../Rev/src/Core/Animator.ixx
@../../Rev/src/GlobalTime.ixx
@../../Rev/src/Core/Shared.ixx
@../../Rev/src/Core/Resource.ixx

## Style & the element tree

@../../Rev/src/Element/Style/Style.ixx
@../../Rev/src/Element/Style/Themes/Default.ixx
@../../Rev/src/Element/Resolved.ixx
@../../Rev/src/Element/Element.ixx
@../../Rev/src/Element/Event/Event.ixx

## Built-in elements (representative set)

@../../Rev/src/Elements/Box.ixx
@../../Rev/src/Elements/Text.ixx
@../../Rev/src/Elements/Controls/ControlTheme.ixx
@../../Rev/src/Elements/Controls/Button/Button.ixx
@../../Rev/src/Elements/Controls/TextInput/TextInput.ixx

## Graphics core (OpenGL backend + base primitives)

@../../Rev/src/Graphics/Primitives/Primitive.ixx
@../../Rev/src/Graphics/Primitives/Rectangle/Rectangle.ixx
@../../Rev/src/Graphics/Primitives/Text/Text.ixx
@../../Rev/src/Graphics/OpenGL/Canvas.OpenGL.ixx
@../../Rev/src/Graphics/OpenGL/CommandBuffer.OpenGL.ixx
@../../Rev/src/Graphics/OpenGL/FrameBuffer.OpenGL.ixx
@../../Rev/src/Graphics/OpenGL/Pipeline.OpenGL.ixx
@../../Rev/src/Graphics/OpenGL/Shader.OpenGL.ixx
@../../Rev/src/Graphics/OpenGL/Texture.OpenGL.ixx
@../../Rev/src/Graphics/OpenGL/UniformBuffer.OpenGL.ixx
@../../Rev/src/Graphics/OpenGL/VertexBuffer.OpenGL.ixx

## Runtime seam (frame loop, event dispatch, native window)

@../../Rev/src/Window.ixx
@../../Rev/src/Native/Windows/Application.win.ixx
@../../Rev/src/Native/Windows/NativeWindow.win.ixx
