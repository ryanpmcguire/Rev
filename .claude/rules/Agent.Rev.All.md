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

## Core — geometry

@../../Rev/src/Core/Geometry/Pos.ixx
@../../Rev/src/Core/Geometry/D3/Pos3.ixx
@../../Rev/src/Core/Rect.ixx
@../../Rev/src/Core/Lrtb.ixx
@../../Rev/src/Core/Line.ixx
@../../Rev/src/Core/Transform.ixx
@../../Rev/src/Core/View.ixx

## Core — color, vertices, resources, reactivity, time

@../../Rev/src/Core/Color.ixx
@../../Rev/src/Core/Vertex.ixx
@../../Rev/src/Core/Vertex3.ixx
@../../Rev/src/Core/Dispatcher.ixx
@../../Rev/src/Core/DirtyFlag.ixx
@../../Rev/src/Core/Observable.ixx
@../../Rev/src/Core/Process.ixx
@../../Rev/src/Core/Animator.ixx
@../../Rev/src/GlobalTime.ixx
@../../Rev/src/Core/Shared.ixx
@../../Rev/src/Core/Resource.ixx
@../../Rev/src/Core/Font.ixx
@../../Rev/src/Core/FontAtlas.ixx
@../../Rev/src/Core/Svg.ixx

## Style & the element tree

@../../Rev/src/Element/Style/Style.ixx
@../../Rev/src/Element/Style/Themes/Default.ixx
@../../Rev/src/Element/Resolved.ixx
@../../Rev/src/Element/Element.ixx
@../../Rev/src/Element/Event/Event.ixx
@../../Rev/src/Element/Event/GestureTracker.ixx

## Built-in elements — primitives-backed widgets

@../../Rev/src/Elements/Box.ixx
@../../Rev/src/Elements/Text.ixx
@../../Rev/src/Elements/Svg.ixx

## Built-in elements — controls

@../../Rev/src/Elements/Controls/ControlTheme.ixx
@../../Rev/src/Elements/Controls/Button/Button.ixx
@../../Rev/src/Elements/Controls/TextInput/TextInput.ixx
@../../Rev/src/Elements/Controls/NumberInput/NumberInput.ixx
@../../Rev/src/Elements/Controls/Slider/Slider.ixx
@../../Rev/src/Elements/Controls/Checkbox/Checkbox.ixx
@../../Rev/src/Elements/Controls/Radio/Radio.ixx
@../../Rev/src/Elements/Controls/Dropdown/Dropdown.ixx
@../../Rev/src/Elements/Controls/Collapsible/Collapsible.ixx

## Built-in elements — display & 3D

@../../Rev/src/Elements/Display/Chart.ixx
@../../Rev/src/Elements/Display/View3D/View3D.ixx
@../../Rev/src/Elements/Display/View3D/Actor3d.ixx
@../../Rev/src/Elements/Display/View3D/Camera3d.ixx

## Graphics — primitives

@../../Rev/src/Graphics/Primitives/Primitive.ixx
@../../Rev/src/Graphics/Primitives/Rectangle/Rectangle.ixx
@../../Rev/src/Graphics/Primitives/Text/Text.ixx
@../../Rev/src/Graphics/Primitives/Svg/Svg.ixx
@../../Rev/src/Graphics/Primitives/Lines/Lines.ixx
@../../Rev/src/Graphics/Primitives/FastLines/FastLines.ixx
@../../Rev/src/Graphics/Primitives/Lines3d/Lines3d.ixx
@../../Rev/src/Graphics/Primitives/Triangles/Triangles.ixx
@../../Rev/src/Graphics/Primitives/Mesh3d/Mesh3d.ixx

## Graphics — OpenGL backend

@../../Rev/src/Graphics/OpenGL/Canvas.OpenGL.ixx
@../../Rev/src/Graphics/OpenGL/CommandBuffer.OpenGL.ixx
@../../Rev/src/Graphics/OpenGL/FrameBuffer.OpenGL.ixx
@../../Rev/src/Graphics/OpenGL/Pipeline.OpenGL.ixx
@../../Rev/src/Graphics/OpenGL/Shader.OpenGL.ixx
@../../Rev/src/Graphics/OpenGL/Texture.OpenGL.ixx
@../../Rev/src/Graphics/OpenGL/TextureBuffer.OpenGL.ixx
@../../Rev/src/Graphics/OpenGL/UniformBuffer.OpenGL.ixx
@../../Rev/src/Graphics/OpenGL/VertexBuffer.OpenGL.ixx

## Graphics — Metal backend

@../../Rev/src/Graphics/Metal/Canvas.Metal.ixx
@../../Rev/src/Graphics/Metal/FrameBuffer.Metal.ixx
@../../Rev/src/Graphics/Metal/Pipeline.Metal.ixx
@../../Rev/src/Graphics/Metal/Shader.Metal.ixx
@../../Rev/src/Graphics/Metal/Texture.Metal.ixx
@../../Rev/src/Graphics/Metal/UniformBuffer.Metal.ixx
@../../Rev/src/Graphics/Metal/VertexBuffer.Metal.ixx

## Graphics — Vulkan backend (WIP)

@../../Rev/src/Graphics/Vulkan/Vulkan.ixx
@../../Rev/src/Graphics/Vulkan/Device.Vulkan.ixx
@../../Rev/src/Graphics/Vulkan/Surface.Vulkan.ixx
@../../Rev/src/Graphics/Vulkan/Swapchain.Vulkan.ixx
@../../Rev/src/Graphics/Vulkan/Renderpass.Vulkan.ixx
@../../Rev/src/Graphics/Vulkan/Framebuffers.Vulkan.ixx
@../../Rev/src/Graphics/Vulkan/CommandPool.Vulkan.ixx
@../../Rev/src/Graphics/Vulkan/Canvas.Vulkan.ixx

## Runtime seam — window & frame loop

@../../Rev/src/Window.ixx

## Native — Windows

@../../Rev/src/Native/Windows/Application.win.ixx
@../../Rev/src/Native/Windows/NativeWindow.win.ixx
@../../Rev/src/Native/Windows/NativeWindow.linux.ixx

## Native — Linux

@../../Rev/src/Native/Linux/Application.lnx.ixx
@../../Rev/src/Native/Linux/NativeWindow.lnx.ixx

## Native — macOS

@../../Rev/src/Native/MacOS/Application.mac.ixx
@../../Rev/src/Native/MacOS/NativeWindow.mac.ixx

## OS — Windows (dialogs, files, serial, sockets)

@../../Rev/src/OS/Windows/Client.win.ixx
@../../Rev/src/OS/Windows/Dialog.win.ixx
@../../Rev/src/OS/Windows/File.win.ixx
@../../Rev/src/OS/Windows/Serial.win.ixx
@../../Rev/src/OS/Windows/Socket.win.ixx

## OS — Linux (dialogs, files, serial, sockets)

@../../Rev/src/OS/Linux/Dialog.lnx.ixx
@../../Rev/src/OS/Linux/File.lnx.ixx
@../../Rev/src/OS/Linux/Serial.lnx.ixx
@../../Rev/src/OS/Linux/Socket.lnx.ixx
