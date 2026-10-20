# Direct3D 12 backend

Initial implementation of Rev's `Canvas`, `Pipeline`, `Shader`, `UniformBuffer`,
`VertexBuffer`, `TextureBuffer`, `Texture`, `FrameBuffer`, `CommandBuffer`, and
`CameraTexture` modules. The `.DirectX.ixx` files export the same module names as
the other graphics backends. Windows/D3D12 implementation details are in `Helpers/`.

The built-in primitive shaders are ported and wired into their pipelines. Before
using this backend for changing application content, integrate direct-write
buffer revisions as described below; raw pointer writes currently need explicit
revision commits after the first upload.

## Resource updates

Buffers expose stable, aligned CPU storage through `data`, valid until resize or
destruction. CPU reads, field writes, and read-modify-write operations are supported.
After direct writes during primitive computation, call `markChanged()` once:

```cpp
data->color = resolvedColor;
databuff->markChanged();
databuff->upload(); // optional explicit preparation after computation
```

`set()`, `set3()`, and size-changing `resize()` calls advance the revision themselves.
`upload()` returns immediately when the uploaded revision matches. `bind()` also
checks the revision, so it can prepare a changed resource if explicit preparation
was omitted. It does not copy unchanged data on every draw. Direct writes without
`markChanged()` after the first upload are intentionally not detected. A primitive's
existing RevisionFlag observer can decide when to call this method.

Each changed buffer revision creates an immutable upload-heap snapshot. Frames
retain the snapshots they reference until their fence completes. Thus an update
does not overwrite data used by an earlier draw, even within the same frame.
This first implementation favors correctness over allocation pooling and default-
heap placement for large geometry. Both optimizations can stay inside the backend.

Textures copy supplied CPU pixels into owned storage. `update()` advances their
revision; their first subsequent bind records the upload on the active command
list. CPU texture preparation can occur before a frame, but texture `upload()`
requires active recording. GPU texture versions and staging allocations remain
alive until submission completes. Static resources share a device across canvases;
cross-canvas texture consumption inserts a GPU queue wait for the producer upload.
Render targets belong to their creating canvas; call `FrameBuffer::finish()` before
sampling their texture. Finish the producer frame before sampling from another canvas.

## Shader contract

Supply HLSL resources using `Pipeline::Params::directXVert` and `directXFrag`.
Both default to entry point `main`; `vertexEntry` and `fragmentEntry` override it.
The existing `definitions` field replaces `DEFINITIONS` or is prepended to source.
Missing HLSL fails with an explicit error; the backend does not translate GLSL.
The compiler is D3DCompile, targeting `vs_5_1` and `ps_5_1` (DXBC supported by D3D12).
Includes are not resolved: supply self-contained/embedded shader sources.

* Uniform binding N maps to `register(bN)`, N = 0..7, visible to both stages.
* Texture/buffer binding N maps to `register(tN)`, N = 0..7.
* Texture samplers map to `register(sN)`, N = 0..7.
* Vertex attributes map to `ATTR0`, `ATTR1`, etc., as 1..4 floats. The bound vertex
  buffer's `divisor` determines the input rate; zero means per vertex.
* `TextureBuffer` is `Buffer<float2>`, matching the OpenGL RG32F texel layout.
* Constant allocations are padded to 256 bytes. Individual C++/HLSL field layouts
  still need matching offsets and HLSL packing. Matrices compile column-major.
* D3D clip-space depth is [0,1]. Canvas's 2D transform accounts for this. Shaders
  using existing OpenGL 3D matrices must convert `z = (z + w) * 0.5` or use an
  appropriate D3D projection, and adjust explicit fragment-depth calculations.
* Triangle fans are converted to indexed triangle lists, preserving vertex IDs.

The other backends' Pipeline parameter structs will also need the DirectX resource
fields when shared primitive initializers start supplying HLSL assets.

## Frames and state

Canvas owns a direct queue, two command allocators, fence-delayed resource retention,
and a flip-discard swap chain. It uses a single-sample RGBA8 intermediate color target
and D24S8 depth/stencil, matching the current OpenGL framebuffer's sample count.
Pipeline states are cached by topology, input rate, and color/stencil/depth settings.
Frame recording and GPU resource binding run on the creating thread; nested canvas
recording is rejected. A shared pipeline binds against the currently recording canvas,
not the window where the pipeline was originally constructed.

CameraTexture accepts packed RGBA8/BGRA8/RGBX8/BGRX8 frames. Capture threads only copy
the newest CPU frame under a mutex. The rendering thread consumes it via `latest()`;
texture binding performs the GPU transfer. Pixel-format interpretation remains a
video-shader responsibility. Other formats fail explicitly instead of being read
as four-byte pixels. Callers must stop capture before destroying CameraTexture.

## Building and validation

Requires a Windows SDK with D3D12 and a C++23 modules compiler. Link `d3d12`, `dxgi`,
`d3dcompiler`, and the application's existing Win32 libraries. The current
NativeWindow module still contains WGL functions, so keep its OpenGL/GLEW linkage
until that module is separately decoupled. The DirectX Canvas does not create a
WGL context or present via WGL.

To select this backend, include `.DirectX.ixx` and exclude the OpenGL, Metal, and
Vulkan implementations. Keep the Windows NativeWindow module. Do not compile
`Tests/*.cpp` into the application. Application configurations have been updated
to exclude `.DirectX.ixx` while they still use OpenGL.

Imager has an opt-in manifest at `Firmware/Imager/App/clever.directx.json`.
From the Litho workspace root, build it with:

```powershell
python Firmware/Imager/App/build.py --graphics directx --build-only
```

Alternatively select the VS Code task **Litho: Build Imager (DirectX)**. Add
`--config release` for a release build. The wrapper resolves the toolchain paths,
uses the separate `.clever/directx` or `.clever/directx-release` cache, and names the
executable `ImagerMailbox.DirectX.exe`. The application's build wrapper controls
the default backend and launch policy; primitive revision integration is still pending.

Run the validation script from a PowerShell with a compatible clang++ available:

```powershell
./Tests/Validate.ps1 -Compiler 'path/to/clang++.exe' -OutputDirectory 'path/to/build'
```

It compiles the real module dependencies and a consumer of the exported API, then
builds and runs a WARP test with the D3D12 debug layer. Tests check actual pixels,
unchanged-revision reuse, successive revisions within one submission, stencil and
depth state, textures, typed buffer SRVs, vertex input, cross-canvas sharing,
in-flight destruction, allocator reuse, and hidden HWND resize/presentation.
Install Windows Graphics Tools if the debug layer is unavailable.

### Primitive shader ports

All ten primitive shader pairs are implemented beside their OpenGL sources as
`.vert.hlsl` / `.frag.hlsl`, including Rectangle's `STENCIL` variant. The shared
primitive pipelines select those resources when using DirectX. Rectangle SDFs,
shadows, opacity, glyph layout, SVG rotation/tinting, video levels and pixel-format
handling, lighting, and both fast-line triangulators retain the OpenGL algorithms.

`scripts/PortDirectXShaders.py` is a deliberately narrow adapter for these sources.
Run it after changing GLSL; `--check` detects stale HLSL. It preserves expression
order and translates stage semantics, matrix multiplication/constructors, texture
access, and language syntax. Constants become `static const` to avoid HLSL's
implicit global uniforms. Canvas uses the original GL projection; vertex output
alone maps clip Z from `[-W,W]` to `[0,W]`. FastLines3d keeps its original NDC depth
throughout geometry generation and its original fragment-depth formula. Existing
explicit fragment depths are already in `[0,1]` and are not converted again.

Validation compiles all 21 stage/definition variants and independently compares
reflected cbuffer bindings, offsets and sizes against GLSL std140 declarations.
Add `-ShaderParity` to `Tests/Validate.ps1` to compare offscreen GLSL rendering
against D3D12 WARP, including depth occlusion, joins, clipping, textured glyphs,
rotated SVGs and four video formats. This requires OpenGL 4.3 and Graphics Tools.
Run the resulting `ShaderParity.exe <Primitives directory> --hardware` to compare
against the default hardware DirectX adapter instead. Debug HLSL retains symbols
with optimization level 1; fully unoptimized FXC fast-line code caused device
removal on WARP in testing.

These are algorithm-equivalent ports, not a promise of bit-identical cross-API
rasterization. The parity test permits three 8-bit channel units of rounding,
with larger differences restricted to at most 0.2% of channels and a matching
reference pixel within one pixel. This accommodates discard/nearest-sampling
boundaries; broad interior differences fail. The original rectangle shader's
zero-width smoothstep cases and exact-zero discard comparison are preserved,
including their floating-point sensitivity, rather than silently redesigned.

## Remaining integration and limitations

* Add compute-boundary revisions for primitive buffers written through raw pointers.
* Complete application-level validation: text, SVG, fast lines, 3D projection/depth,
  nested clipping, DPI behavior, and actual camera hardware.
* Mipmapped filters explicitly throw; mip generation and MSAA are not implemented.
* Per frame, each canvas allows 16,384 SRV bindings and 2,048 sampler bindings.
  Exhaustion throws rather than overwriting descriptors in flight. Descriptor
  reuse/pooling is a future optimization.
* Device removal and submission failures report errors; automatic recovery is not
  implemented. Exceptions during frame recording should terminate that render
  session, not resume it. There is no command-list cancellation/replay mechanism.
* Resource construction contexts must remain valid for operations that create new
  wrappers (including CameraTexture publication and framebuffer resize).
* The existing CommandBuffer vocabulary is retained; actual D3D command recording
  lives in Canvas. There is no separate replay interpreter for that vocabulary.
* This does not switch the legacy CMake project to DirectX or repair its existing
  source-root configuration. Its backend filter recognizes DirectX so the new
  module implementations are not accidentally selected alongside another backend.

References: [root signatures](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-a-root-signature),
[fence-based resource management](https://learn.microsoft.com/en-us/windows/win32/direct3d12/fence-based-resource-management).
