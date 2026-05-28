# Linux Build Notes

This branch builds the CAM application on Linux with C++23 modules, Clang, Ninja, X11/GLX, and system OpenCASCADE packages.

## Required tools

Use Ninja. CMake's Unix Makefiles generator does not support this project's C++ module setup reliably.

Known-good toolchain:

- CMake 4.3.2 or newer
- Clang 18 / clang++-18
- `clang-scan-deps-18`
- Ninja

## Required packages

Package names vary by distribution, but the build needs development packages for:

- X11, Xrandr, Xext
- OpenGL
- GLEW
- Freetype
- OpenCASCADE
- Tcl/Tk
- TBB

On Debian/Ubuntu-style systems this usually means packages similar to:

```bash
sudo apt install \
  clang-18 clang-tools-18 ninja-build \
  libx11-dev libxrandr-dev libxext-dev \
  libgl-dev libglew-dev libfreetype-dev \
  libocct-*-dev tk-dev tcl-dev libtbb-dev
```

The OpenCASCADE CMake package should provide a config file like:

```text
/usr/lib/x86_64-linux-gnu/cmake/opencascade/OpenCASCADEConfig.cmake
```

The CAM/OpenCASCADE link also expects libraries such as:

```text
/usr/lib/x86_64-linux-gnu/libtk.so
/usr/lib/x86_64-linux-gnu/libtcl.so
/usr/lib/x86_64-linux-gnu/libtbb.so
/usr/lib/x86_64-linux-gnu/libtbbmalloc.so
```

## Optional runtime package

Linux file/folder/message dialogs currently use `zenity` when available:

```bash
sudo apt install zenity
```

Without `zenity`, some dialogs fall back to terminal logging or cancel-like behavior.

## Configure

```bash
~/.local/bin/cmake -S . -B build-linux \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang-18 \
  -DCMAKE_CXX_COMPILER=clang++-18 \
  -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-18
```

If your CMake is installed elsewhere, replace `~/.local/bin/cmake` with the appropriate path.

## Build

```bash
~/.local/bin/cmake --build build-linux -j$(nproc)
```

## Run

```bash
./build-linux/CAM/CAMDemo
```

## Runtime notes

The current Linux backend is X11/GLX. On Wayland sessions it is expected to run through XWayland. Native Wayland support is not implemented yet.

The backend intentionally uses the legacy GLX context creation path because the modern `glXCreateContextAttribsARB` path previously reproduced resize/presentation artifacts on the tested system.
