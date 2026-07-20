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

## Status

`LithoRev` builds, links, and runs end-to-end on Linux (verified under WSL2
Debian and natively on Linux Mint XFCE). The GDI+-only image decode/drawing
and `OPENFILENAMEA` file dialog that originally blocked the Linux build have
been replaced with cross-platform equivalents (`ImageDecode.ixx` via
`stb_image`, `TestPatternRaster.ixx` for the status dial/marquee, and
`Rev::OS::Dialog`, which now has both a Windows and Linux implementation).

## Known remaining gaps

- **EDID override** (`Demo/source/LithoControl/EdidApply.win.cpp`) is a
  Windows-only feature (CRU-based) with no Linux equivalent implemented —
  `EdidApply.lnx.cpp` is a stub that logs and no-ops. A Linux projector-mode
  override would need to go through `xrandr` separately.
- **E-STOP thread cancellation** (`Rev::OS::ThreadControl`,
  `pthread_kill`/`SIGUSR1`-based on Linux vs. `CancelSynchronousIo` on
  Windows) has not been hardware-tested on Linux. Verify it actually
  interrupts a blocking serial read before relying on it for a live
  gantry/laser session.

Full technical detail (toolchain, per-platform module split, every bug found
during the port) lives in the top-level `CLAUDE.md` under "Linux/XFCE port".
