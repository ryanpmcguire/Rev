# Linux Build Notes (LithoRev)

The Rev framework's platform layer (window, application loop, serial, TCP
client) builds on Linux via X11/GLX. This is ported from the `CAM-linux`
branch's platform work; the `lithorev-linux` branch keeps only the pieces
LithoControl needs (no OpenCASCADE/CAM 3D dependencies).

## Required tools

Use Ninja. CMake's Unix Makefiles generator does not support this project's
C++ module setup reliably.

Known-good toolchain:

- CMake 4.3.2 or newer
- Clang 18 / clang++-18
- `clang-scan-deps-18`
- Ninja

## Required packages

Package names vary by distribution, but the build needs development packages
for:

- X11, Xrandr, Xext
- OpenGL
- GLEW
- Freetype

On Debian/Ubuntu-style systems:

```bash
sudo apt install \
  clang-18 clang-tools-18 ninja-build \
  libx11-dev libxrandr-dev libxext-dev \
  libgl-dev libglew-dev libfreetype-dev
```

## Optional runtime package

Linux file/folder/message dialogs use `zenity` when available:

```bash
sudo apt install zenity
```

Without `zenity`, dialogs fall back to terminal logging or cancel-like
behavior (`Rev::OS::Dialog`, `Rev/src/OS/Linux/Dialog.lnx.ixx`).

## Configure

```bash
~/.local/bin/cmake -S . -B build-linux \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang-18 \
  -DCMAKE_CXX_COMPILER=clang++-18 \
  -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-18
```

If your CMake is installed elsewhere, replace `~/.local/bin/cmake` with the
appropriate path.

## Build

```bash
~/.local/bin/cmake --build build-linux -j$(nproc)
```

## Run

```bash
./build-linux/Demo/LithoRev
```

## Runtime notes

The Linux backend is X11/GLX. On Wayland sessions it is expected to run
through XWayland; native Wayland support is not implemented.

## Known remaining blocker: GDI+

`Demo/source/LithoControl/Interface.ixx` and `ImagePreview.ixx` use Win32
GDI+ (`<gdiplus.h>`) directly for image decoding/scaling and for immediate-
mode drawing of the status dial and marquee text, plus `OPENFILENAMEA` for
the file-open dialog. None of this is behind a Rev abstraction yet, so it
will not compile on Linux as-is. Options to unblock:

- Replace GDI+ image loading/scaling with `stb_image` (already vendorable)
  feeding `Rev.Primitive.Image` / `Rev.Graphics.Texture`.
- Replace the GDI+ dial/marquee drawing with Rev's own primitives
  (`Rectangle`, `Text`, etc.) instead of rasterizing to an offscreen
  `Gdiplus::Bitmap`.
- Replace `OPENFILENAMEA` with `Rev::OS::Dialog` (already ported for Linux
  in `Rev/src/OS/Linux/Dialog.lnx.ixx`; needs a thin Windows counterpart for
  parity).

This is app-level work, separate from the platform-layer port in this
branch.
