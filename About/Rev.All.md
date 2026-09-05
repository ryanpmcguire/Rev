# Agent context — Rev (Everything)

**What this is.** The *entire* Rev framework — every documentation file and every source
module, across all three GPU backends (OpenGL / Vulkan / Metal), all platform layers
(Windows / Linux / macOS), every built-in control, and the full 3D stack — assembled into
one importable unit. The whole framework fits comfortably in an agent's context window;
this file is what makes "load all of Rev at once" a single `@`-include.

**When to load it.** Use this when a task genuinely needs global knowledge of the framework
— a cross-cutting refactor, porting/adding a backend, anything that spans layers you can't
predict in advance. For routine work, prefer `rev-core.md` (always loaded) or
`Agent.Rev.Digest.md` (the curated core); they are far cheaper.

**How to read it.** The `About/` docs (top of the list) give the model; the source is
grouped by layer below. Treat all of it as known, current truth. Paths are relative to this
file (`.claude/rules/`).

---

## Concepts — the About docs (read these first)

@../source/About/Rev.About.md
@../source/About/Rev.Core.md
@../source/Core/ReadMe/Rev.Core.RevisionFlag.md
@../source/About/Rev.Style.md
@../source/About/Rev.Graphics.md
@../source/Element/About/Element.About.md
@../source/Element/About/Element.Api.md
@../source/Element/About/Element.Event.About.md
@../source/Element/About/Element.Events.md
@../source/Element/About/Rev.Element.HowTo.md
@../source/Graphics/About/Graphics.Primitives.md

## Essence — what Rev really is, and how to really use it

The disposition behind the model: read these to understand *why* Rev is shaped the way it
is and how to work with the grain (most Rev mistakes are disposition mistakes, not API
mistakes). Start with the README.

@Essence/What-Rev-Really-Is.md
@Essence/How-To-Really-Use-Rev.md
@Essence/The-Escape-Staircase.md
@Essence/Dos-And-Donts.md

## Core — geometry

@../source/Core/Geometry/Pos.ixx
@../source/Core/Geometry/D3/Pos3.ixx
@../source/Core/Rect.ixx
@../source/Core/Lrtb.ixx
@../source/Core/Line.ixx
@../source/Core/Transform.ixx
@../source/Core/View.ixx

## Core — color, vertices, resources, reactivity, time

@../source/Core/Color.ixx
@../source/Core/Vertex.ixx
@../source/Core/Vertex3.ixx
@../source/Core/Dispatcher.ixx
@../source/Core/DirtyFlag.ixx
@../source/Core/RevisionFlag.ixx
@../source/Core/Observable.ixx
@../source/Core/Process.ixx
@../source/Core/Animator.ixx
@../source/GlobalTime.ixx
@../source/Core/Shared.ixx
@../source/Core/Resource.ixx
@../source/Core/Font.ixx
@../source/Core/FontAtlas.ixx
@../source/Core/Svg.ixx

## Style & the element tree

@../source/Element/Style/Style.ixx
@../source/Element/Style/Themes/Default.ixx
@../source/Element/Resolved.ixx
@../source/Element/Element.ixx
@../source/Element/Event/Event.ixx
@../source/Element/Event/GestureTracker.ixx

## Built-in elements — primitives-backed widgets

@../source/Elements/Box.ixx
@../source/Elements/Text.ixx
@../source/Elements/Svg.ixx

## Built-in elements — controls

@../source/Elements/Controls/ControlTheme.ixx
@../source/Elements/Controls/Button/Button.ixx
@../source/Elements/Controls/TextInput/TextInput.ixx
@../source/Elements/Controls/NumberInput/NumberInput.ixx
@../source/Elements/Controls/Slider/Slider.ixx
@../source/Elements/Controls/Checkbox/Checkbox.ixx
@../source/Elements/Controls/Radio/Radio.ixx
@../source/Elements/Controls/Dropdown/Dropdown.ixx
@../source/Elements/Controls/Collapsible/Collapsible.ixx

## Built-in elements — display & 3D

@../source/Elements/Display/Chart.ixx
@../source/Elements/Display/Camera.ixx
@../source/Elements/Display/View3D/View3D.ixx
@../source/Elements/Display/View3D/Actor3d.ixx
@../source/Elements/Display/View3D/Camera3d.ixx

## Graphics — primitives

@../source/Graphics/Primitives/Primitive.ixx
@../source/Graphics/Primitives/Rectangle/Rectangle.ixx
@../source/Graphics/Primitives/Text/Text.ixx
@../source/Graphics/Primitives/Svg/Svg.ixx
@../source/Graphics/Primitives/Lines/Lines.ixx
@../source/Graphics/Primitives/FastLines/FastLines.ixx
@../source/Graphics/Primitives/Lines3d/Lines3d.ixx
@../source/Graphics/Primitives/Triangles/Triangles.ixx
@../source/Graphics/Primitives/Mesh3d/Mesh3d.ixx
@../source/Graphics/Primitives/Video/Video.ixx

## Media capture

@../source/OS/Media/Camera/Camera.win.ixx
@../source/OS/Media/Camera/Camera.lnx.ixx
@../source/OS/Media/Camera/Camera.mac.ixx

## Graphics — OpenGL backend

@../source/Graphics/OpenGL/Canvas.OpenGL.ixx
@../source/Graphics/OpenGL/CommandBuffer.OpenGL.ixx
@../source/Graphics/OpenGL/FrameBuffer.OpenGL.ixx
@../source/Graphics/OpenGL/Pipeline.OpenGL.ixx
@../source/Graphics/OpenGL/Shader.OpenGL.ixx
@../source/Graphics/OpenGL/Texture.OpenGL.ixx
@../source/Graphics/OpenGL/TextureBuffer.OpenGL.ixx
@../source/Graphics/OpenGL/UniformBuffer.OpenGL.ixx
@../source/Graphics/OpenGL/VertexBuffer.OpenGL.ixx

## Graphics — Metal backend

@../source/Graphics/Metal/Canvas.Metal.ixx
@../source/Graphics/Metal/FrameBuffer.Metal.ixx
@../source/Graphics/Metal/Pipeline.Metal.ixx
@../source/Graphics/Metal/Shader.Metal.ixx
@../source/Graphics/Metal/Texture.Metal.ixx
@../source/Graphics/Metal/UniformBuffer.Metal.ixx
@../source/Graphics/Metal/VertexBuffer.Metal.ixx

## Graphics — Vulkan backend (WIP)

@../source/Graphics/Vulkan/Vulkan.ixx
@../source/Graphics/Vulkan/Device.Vulkan.ixx
@../source/Graphics/Vulkan/Surface.Vulkan.ixx
@../source/Graphics/Vulkan/Swapchain.Vulkan.ixx
@../source/Graphics/Vulkan/Renderpass.Vulkan.ixx
@../source/Graphics/Vulkan/Framebuffers.Vulkan.ixx
@../source/Graphics/Vulkan/CommandPool.Vulkan.ixx
@../source/Graphics/Vulkan/Canvas.Vulkan.ixx

## Runtime seam — window & frame loop

@../source/Window.ixx

## OS — application and window backends

@../source/OS/Application/Application.win.ixx
@../source/OS/Application/Application.lnx.ixx
@../source/OS/Application/Application.mac.ixx
@../source/OS/Window/NativeWindow.win.ixx
@../source/OS/Window/NativeWindow.lnx.ixx
@../source/OS/Window/NativeWindow.mac.ixx

## OS — Windows (dialogs, files, serial, sockets)

@../source/OS/Windows/Client.win.ixx
@../source/OS/Windows/Dialog.win.ixx
@../source/OS/Windows/File.win.ixx
@../source/OS/Windows/Serial.win.ixx
@../source/OS/Windows/Socket.win.ixx

## OS — Linux (dialogs, files, serial, sockets)

@../source/OS/Linux/Dialog.lnx.ixx
@../source/OS/Linux/File.lnx.ixx
@../source/OS/Linux/Serial.lnx.ixx
@../source/OS/Linux/Socket.lnx.ixx
