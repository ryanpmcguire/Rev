# CLAUDE.md

This file provides guidance to Claude Code when working in the Rev repository.

## Project Overview

Rev is a GPU-accelerated C++23 GUI framework. It uses C++20 modules (`.ixx` files), a flex-style layout engine, and native platform backends (Win32 + OpenGL on Windows, X11 + OpenGL on Linux, Cocoa + Metal on macOS). The primary use case driving active development is a **DLP photolithography control GUI** (`Demo/source/LithoControl/Interface.ixx`).

## Build system

- **CMake 3.26+** on Windows (MSVC's module support); **CMake ≥4.3.2** on Linux/Clang — older apt-packaged cmake (e.g. Debian trixie's 3.31.6) can't do C++20 module dependency scanning correctly. If apt's version is too old: `pip install --user --break-system-packages cmake`.
- **MSVC 2022+** for Windows; **Clang 18** (via Ninja) for Linux; Apple Clang for macOS
- Source files are collected with `GLOB_RECURSE` — **adding a new `.ixx` file anywhere under `Rev/src/` or `Demo/source/` is automatically picked up, no CMakeLists edit needed** (exception: new link libraries must be added manually)
- Platform filter: files named `*.win.ixx` compile on Windows only, `*.lnx.ixx` on Linux only, `*.mac.ixx` on macOS only. The filter regex only recognizes simple two-segment names (`Name.marker.ext`) — a three-segment name like `Foo.Bar.win.cpp` silently loses its platform marker and gets compiled on every platform. Keep new platform-specific files to the two-segment form.

```
# Windows
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target HelloWorld

# Linux
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
  -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=$(command -v clang-scan-deps-18)
cmake --build build-linux --target LithoRev -j"$(nproc)"
```

### Linux/XFCE port (branch `lithorev-linux`)

`LithoRev` builds and links successfully on Linux (Clang 18 + Ninja, verified both under WSL2 Debian and natively on Linux Mint XFCE). Notes specific to this port:

- **Header hygiene**: MSVC's standard library headers transitively pull in `<algorithm>`, `<cstdint>`, `<filesystem>`, `<cmath>` etc. in places Clang/libstdc++ don't. Any "no member named X in namespace std" or "use of undeclared identifier" error on Linux that looks like a real standard-library symbol is almost always a missing include that MSVC was silently tolerating — add the include rather than looking for a logic bug.
- **`Window` name collision**: X11's `Xlib.h` defines a global `Window` typedef (`typedef XID Window`) that collides with `Rev::Window` wherever a file has `using namespace Rev;` in scope (e.g. any `LithoControl.Interface` implementation unit). Qualify X11 window handles as `::Window` in those files.
- **`Rev::Application::windows`** must be `std::vector<Window*>` on every platform (matching the `Window(std::vector<Window*>&, Details)` constructor) — it was `std::vector<void*>` on Linux only and broke `main.cpp`.
- **`Rev.OS.SerialPort::List()` on Linux**: `/dev/ttyS0`–`ttyS31` (legacy platform 8250 UARTs) are always present in `/sys/class/tty` with a `device` symlink regardless of real hardware, so a plain "has a device symlink" check isn't enough to filter them out. Uses the same `TIOCGSERIAL`/`PORT_UNKNOWN` probe as pyserial's `comports()` to skip phantom `ttyS*` ports; `ttyUSB*`/`ttyACM*` are trusted as-is.
- **Known gaps, not ported**:
  - `EdidApply.lnx.cpp` is a stub — CRU-based EDID override has no Linux equivalent; a Linux projector-mode override would need to go through `xrandr` separately.
  - **E-STOP thread cancellation** (`Rev::OS::ThreadControl`, `pthread_kill`/`SIGUSR1`-based on Linux vs. `CancelSynchronousIo` on Windows) has not been hardware-tested. Verify before trusting it on a live gantry/laser session.
- **Runtime deps on the target machine**: `libx11-dev libxext-dev libxrandr-dev libgl1-mesa-dev libglew-dev libfreetype-dev python3 xclip` (or `xsel`) via apt, plus `clang-tools-18` if `clang-scan-deps-18` isn't already on `PATH`.
- `PROJECT_ROOT` is baked in at compile time as `CMAKE_SOURCE_DIR` (used to locate `Rev/resources/Fonts/Roboto/Roboto.ttf` and by `Resource.ixx`'s dev-mode file resolution) — build **on** the target machine, at whatever path you actually intend to run from, rather than copying a binary built elsewhere at a different path.

## Module naming convention

| Path | Module name | Pattern |
|---|---|---|
| `Rev/src/Elements/Box.ixx` | `Rev.Element.Box` | `export module Rev.Element.Box;` |
| `Rev/src/OS/Serial.ixx` | `Rev.Serial` | `export module Rev.Serial;` |
| `Rev/src/OS/SocketClient.ixx` | `Rev.SocketClient` | `export module Rev.SocketClient;` |
| `Demo/source/LithoControl/Interface.ixx` | `LithoControl.Interface` | `export module LithoControl.Interface;` |

## Style system key facts

- `Style` is a plain aggregate — set fields with designated initialisers
- `.applies = { .hover = true }` makes a style conditional on element state
- `.transition = 100_ms` on any field animates changes to that field
- Dimension literals: `100_px`, `100_pct`, `Grow()`, `Px(n)`, `Pct(n)`
- Colours: `rgba(r,g,b,a)`, `rgb(r,g,b)` where r/g/b are 0–255 and a is 0–1
- Pass styles at construction: `new Box(parent, { &baseStyle, &hoverStyle })`
- Or mutate inline: `box->style->background.color = rgba(0, 87, 255, 1)`

### Axis::Vertical layout field semantics (non-obvious)

For a container with `layout.direction = Axis::Vertical`:
- `layout.horizontal` = **cross-axis** (left/right alignment of children)
- `layout.vertical` = **main-axis** (top/bottom distribution of children — Start, End, Center)

This is the opposite of what the field name implies. `Align::Start` on `layout.vertical` packs children to the top. `Align::End` pushes children to the bottom of the container's layout rect.

### Wrap::False is required for vertical scroll containers

Always set `layout.wrap = Wrap::False` on vertically-scrolling content columns. `Wrap::True` (the default) causes the layout engine to create additional columns when children overflow, breaking scroll. Symptom: content appears wider than the parent and scroll stops working.

```cpp
sidebarContent->style->layout.direction  = Axis::Vertical;
sidebarContent->style->layout.wrap       = Wrap::False;   // required
sidebarContent->style->layout.position   = Position::Absolute;
```

### Position::Absolute coordinate system

In Rev, absolute positioning is relative to the **direct parent's rect**, not the nearest positioned ancestor (unlike CSS). `position.top = 100_pct` resolves against the direct parent's `size.h.val`:

```cpp
// optionsContainer is a child of `dropdown`; top=100_pct positions it just below
optionsContainer->style->position.top = 100_pct;  // = dropdown.rect.h below dropdown.rect.y
```

### Overflow::Hide clips visuals but not hit-testing

`Overflow::Hide` on a parent sets an OpenGL stencil that clips drawing. However, `setTargets()` uses `elem.rect.contains(pos)` without checking whether the rect falls inside an overflow-clipping ancestor. Elements that render outside an `Overflow::Hide` parent may be invisible but still receive mouse events.

## Element lifecycle

- Elements are heap-allocated, parented via constructor: `new Text(parent, "hello")`
- `delete element` is safe and removes the element from its parent's children
- **Never create or delete elements from a background thread** — do it in `computeChildren` or `computeStyle`, or post via a thread-safe queue
- `computeStyle(Event& e)` — called every frame, safe to update style properties and observable content
- `computeChildren(Event& e)` — called when child list may need rebuilding; gate rebuilds with a dirty flag
- Always call the parent `computeStyle` / `computeChildren` at the end: `Box::computeStyle(e)`

### Frame execution order (per draw call)

```
computeChildrenTopDown   ← tree structure changes here (addChild/removeChild)
calculateQueues          ← rebuilds topDown/bottomUp traversal lists
computeStyle (topDown)   ← style property updates here
resolveStyle             ← merges active conditional styles into resolved.style
cascadeStyle             ← propagates resolved.hidden down the tree
calcFlexLayouts:
  resetResolved (topDown) ← zeroes all resolved sizes and rects
  resolveMinima (bottomUp)
  resolveMaxima (topDown)
  resolveLayout (bottomUp) ← builds row.members; hidden elements INCLUDED
  resolveDims (topDown)
  resolveRects (topDown)  ← assigns final rect.x/y/w/h
computePrimitives (topDown) ← GPU primitive data set from rect
draw                     ← renders; skips resolved.hidden elements
setTargets               ← called on mouse events; skips resolved.hidden elements
```

Key consequence: **do not modify the element tree in `computeStyle`**. The `computeChildrenTopDown` pass has already finished; adding/removing children during the style pass corrupts the in-progress traversal and crashes.

### Visibility::Hidden vs. addChild/removeChild (slot pattern)

`Visibility::Hidden` sets `resolved.hidden = true` and causes `resolveMinima`, `resolveDims`, and `resolveRects` to return early for the element — resulting in **0 width, 0 height, and zero margin contribution**. The element is therefore invisible and occupies no layout space. However, it **remains in `row.members`** from the parent's `resolveLayout` pass, which runs before hidden-state is considered.

This can cause subtle hit-area offset bugs when an element transitions between hidden and visible states. The safe and proven pattern is the **slot + addChild/removeChild**:

```cpp
// In build function: create slot, build row as child, immediately remove
Box* mySlot = new Box(parent);
mySlot->style->layout.direction = Axis::Vertical;
mySlot->style->size.width = 100_pct;

Box* myRow = new Box(mySlot);
// ... populate myRow ...
mySlot->removeChild(myRow);  // start hidden

// In computeChildren: toggle presence
bool wantVisible = someCondition;
auto& kids = mySlot->children;
bool inSlot = std::find(kids.begin(), kids.end(), (Element*)myRow) != kids.end();
if (wantVisible && !inSlot)  mySlot->addChild(myRow);
if (!wantVisible && inSlot)  mySlot->removeChild(myRow);
```

This is exactly how `platformRowSlot`, `sendChkSlot`, and `hdmiDisplaySlot` are implemented in LithoControl.

### Measuring content height for scroll (measureSpread)

An absolutely-positioned container's own `rect.h` is clamped to its parent (since it's in absolute flow). To measure the actual spread of its children for scroll range calculations, use `measureSpread(element)` — it returns `max(child.rect.y + child.rect.h) - element.rect.y` across all children.

```cpp
float contentH = measureSpread(sidebarContent);
float maxScroll = std::max(0.0f, contentH - sidebarBox->rect.h);
```

Do **not** use `sidebarContent->rect.h` — it will always equal the sidebar viewport height, making `maxScroll` always 0.

## Observable<T>

- `text->content = "new value"` sets content and marks the element dirty
- `text->strContent` reads the current string value directly
- `Observable<bool>` (e.g. `Checkbox::value`): assign with `=`, read with `if (value)` or `(bool)value`
- `value.changed()` / `value.changedFlag` for change detection in `computeStyle`

## Background threading pattern

Rev has no built-in signal/slot mechanism. The correct pattern for background work:

```cpp
struct MsgQueue {
    std::mutex mtx; std::deque<std::string> q;
    void push(std::string s) { std::lock_guard g(mtx); q.push_back(std::move(s)); }
    bool pop(std::string& out) {
        std::lock_guard g(mtx);
        if (q.empty()) return false;
        out = std::move(q.front()); q.pop_front(); return true;
    }
};

// Background thread pushes to queue
// computeStyle drains queue and updates UI elements (main thread)
void computeStyle(Event& e) override {
    std::string msg;
    while (msgQ.pop(msg)) { logText->content = msg; }
    Box::computeStyle(e);
}
```

## OS modules

Each `Rev.OS.*` module is split per-platform (`Windows/Foo.win.ixx`, `Linux/Foo.lnx.ixx`, `MacOS/Foo.mac.ixx` where it exists) behind one shared module name/interface, selected at compile time by the platform file filter above.

### `Rev.Serial` (`OS/Windows/Serial.win.ixx`, `OS/Linux/Serial.lnx.ixx`)

Serial port I/O — Win32 `CreateFile` on Windows, `termios` on Linux. Constructor opens port; destructor closes handle/fd.

- `sendBytes(data, n)` — write raw bytes
- `sendText(str)` — write string
- `sendByte(b)` — write one byte
- `readLine(timeoutMs=5000)` — reads until `\n`, strips `\r`, returns `std::string`
- `readBytes(n, timeoutMs=5000)` — reads exactly n bytes, returns `std::vector<uint8_t>`
- `connected()` — true if handle is valid

**Timeout notes:** `readLine` / `readBytes` temporarily set the read timeout to the given value (Windows: `ReadTotalTimeoutConstant`; Linux: `VTIME`/`poll`), then restore the default. Always call from a background thread.

### `Rev.SocketClient` (`OS/Windows/SocketClient.win.ixx`, `OS/Linux/SocketClient.lnx.ixx`)

TCP client — WinSock2 on Windows, BSD sockets on Linux. Connects synchronously with 5-second timeout; receives lines asynchronously on an internal thread.

- `connect(host, port)` — resolves hostname, connects, starts receive thread; returns `bool`
- `sendLine(msg)` — sends `msg + "\n"`
- `disconnect()` — shuts down socket, joins thread; safe to call from any thread
- `isConnected()` — true if socket is valid
- Callback fires on the receive thread — post to a `MsgQueue` rather than touching UI directly

### `Rev.OS.SerialPort` (`OS/Windows/SerialPort.win.ixx`, `OS/Linux/SerialPort.lnx.ixx`)

`List()` returns available serial ports for the COM-port dropdown. Windows: SetupAPI enumeration. Linux: walks `/sys/class/tty`, filtering out phantom `ttyS0..31` via `TIOCGSERIAL` (see Linux port notes above) and reading `manufacturer`/`product` sysfs attributes for the friendly label.

### `Rev.OS.Display` (`OS/Windows/Display.win.ixx`, `OS/Linux/Display.lnx.ixx`)

`List()`/`SetMode()` for display enumeration and mode-setting. Windows: `EnumDisplayDevicesA`/`ChangeDisplaySettingsExA`. Linux: reuses `Rev::NativeWindow::getDisplays()` (XRandR) for listing, shells out to `xrandr` for `SetMode()`.

### `Rev.OS.ThreadControl` (`OS/Windows/ThreadControl.win.ixx`, `OS/Linux/ThreadControl.lnx.ixx`)

Safety-critical E-STOP mechanism — interrupts a blocking I/O call on another thread. `CurrentThreadHandle()`, `CancelBlockingIo(handle)`, `ReleaseThreadHandle(handle)`. Windows: `CancelSynchronousIo`. Linux: `pthread_kill(SIGUSR1)` with a `sigaction` handler that has deliberately **no** `SA_RESTART` (so `EINTR` propagates instead of the syscall auto-restarting), plus a `thread_local` flag (`ConsumeCancelFlag()`) so a read loop can distinguish a real cancellation from a spurious `EINTR`. **Not hardware-tested on Linux** — verify before relying on it for a live E-STOP.

### `Rev.OS.Clipboard` (`OS/Windows/Clipboard.win.ixx`, `OS/Linux/Clipboard.lnx.ixx`)

`SetText(text)`. Windows: `OpenClipboard`/`SetClipboardData`. Linux: shells out to `xclip -selection clipboard`, falling back to `xsel --clipboard --input` (there's no native clipboard API on X11 without becoming a selection owner and answering `SelectionRequest` events asynchronously).

### `Rev.OS.Dialog` (`OS/Windows/Dialog.win.ixx`, `OS/Linux/Dialog.lnx.ixx`)

File/folder pickers and message boxes. Linux implementation shells out to `zenity`.

## LithoControl Interface

`Demo/source/LithoControl/Interface.ixx` exports `LithoControl::Interface : public Box`.

#### Show/hide patterns used in LithoControl

| Element | Technique | Reason |
|---|---|---|
| `stmPortRow` / `piHostRow` | `addChild`/`removeChild` on `platformRowSlot` | One-at-a-time swap without layout shift |
| `sendTargetChk` | `addChild`/`removeChild` on `sendChkSlot` | Pi-only; hides on STM32 platform |
| `hdmiDisplayRow` | `addChild`/`removeChild` on `hdmiDisplaySlot` | Toggled by HDMI passthrough checkbox; `Visibility::Hidden` caused a click-offset equal to the row height |

All three use the same slot pattern. Do not revert to `Visibility::Hidden` for these — the hit-area offset bug returns.

#### Sidebar scrolling implementation

The sidebar uses a manually-driven scroll (no Rev scrollbar widget) because the scrollable content is an absolutely-positioned `sidebarContent` box shifted by `position.top = Px(-sidebarScrollY)`. Key points:

- `Overflow::Hide` on `sidebarBox` clips the content visually
- `sidebarScrollY` is updated immediately in the `onMouseWheel` handler and also clamped in `computeStyle` each frame
- `measureSpread(sidebarContent)` is the only correct way to get content height (see above)
- The scrollbar thumb is an absolutely-positioned child of `sidebarScrollTrackBox`; its position and size are computed from `sidebarScrollY / maxScroll` in `computeStyle`

### Dependencies for LithoControl

`Demo/CMakeLists.txt` links `gdiplus comdlg32 shell32` on Windows (Win32 system libraries, no install required) and `X11 GLEW freetype pthread` on Linux (`libx11-dev libglew-dev libfreetype-dev` via apt), gated behind `WIN32`/platform checks in the CMakeLists.

### Key design decisions

- `pngToBitmap(path)` uses GDI+ (`Gdiplus::Bitmap::LockBits`) on Windows to decode PNG frames and pack them into 28,800-byte 1bpp MSB-first bitmaps for the STM32 `PATTERN` command
- Slicer runs as a subprocess (`Interface::runCapturedProcess` → `pc/slicer.py`; Windows: `CreateProcess`+pipe, Linux: `popen`) so the C++ GUI does not need Pillow/numpy. Invokes `python` on Windows, `python3` on Linux (most distros don't alias `python` → `python3`).
- Settings stored via a small hand-rolled `[section]`/`key=value` INI reader/writer (`readIniSection`/`writeIniSection`, `<fstream>` only) at `%APPDATA%\LithoControl\settings.ini` on Windows, `~/.config/LithoControl/settings.ini` on Linux
- `refreshJobList()` scans the local jobs directory for subdirs containing `manifest.json`
- The `selectedJob` string tracks the currently highlighted job across `rebuildJobList()` calls
- HDMI/projector passthrough window (`HdmiWindow.win.cpp` / `HdmiWindow.lnx.cpp`) bypasses Rev's own window abstraction entirely — raw Win32 (`CreateWindowExW`+GDI blit) or raw Xlib (`override_redirect` window + `XPutImage`) — because neither platform's `Rev::Window` exposes borderless-topcanless-blit primitives it needs
- Camera capture (`CameraCapture.win.cpp` / `CameraCapture.lnx.cpp`): Media Foundation `IMFSourceReader` on Windows, V4L2 mmap'd buffer I/O + `poll()` on Linux (hand-written BT.601 YUYV→RGBA conversion since V4L2 has no automatic format-conversion pipeline like Media Foundation's video processor MFT)
- AmScope MU130 camera (Windows only): captured via the vendor "amcam" SDK (`Rev.AmcamCamera`, `Rev/src/OS/Windows/AmcamCamera.win.ixx`) instead of generic MediaFoundation/UVC, mirroring the call sequence already validated in `calib-dt/digital_twin/src/camera/amcam.py` (`Amcam_EnumV2` → `Amcam_Open` → `Amcam_get_Size` → best-effort AutoExpo → `Amcam_StartPullModeWithCallback` → `Amcam_PullImageV2` per frame). `amcam.dll` is resolved at runtime via `LoadLibrary` from the fixed install path `C:\Program Files\AmScope\AmScope\x64\amcam.dll` — no `amcam.h`/`amcam.lib` is vendored (none ships with the installed software), so all entry points are pulled by name via `GetProcAddress`, matching the Python ctypes approach. Wired into the existing camera dropdown/preview pipeline in `CameraCapture.win.cpp`: `scanCameras()` appends AmScope devices as `"amcam:<id>"` options alongside the MediaFoundation ones (now `"mf:<idx>"`); `startCamera()` branches on that prefix into `runCameraCaptureAmcam()`. Deliberately added *inside* the existing `CameraCapture.win.cpp` implementation unit rather than as a new `AmcamCapture.win.cpp` file — see the `EdidApply`/4th-implementation-unit note above; a 4th Windows implementation unit of `LithoControl.Interface` is known to reproducibly hit an MSVC C++20-modules internal compiler error.

### STM32 serial protocol (from `litho_runner.py` notes)

| Command | Response | Notes |
|---|---|---|
| `PING\n` | `PONG\n` | Connection health check |
| `PATTERN\n` | `READY\n` | Begin 28800-byte bitmap + 1-byte XOR checksum transfer |
| `[bitmap][checksum]` | `OK\n` | Bitmap received and checksummed |
| `EXPOSE <ms>\n` | `EXPOSING\n` → `DONE\n` / `ABORTED\n` | Expose loaded pattern |
| `BLANK\n` | `OK\n` | Clear DMD |
| `FILL\n` | `OK\n` | All pixels on |
| `UV_ON\n` | `OK\n` | PG4 HIGH — enable UV laser driver |
| `UV_OFF\n` | `OK\n` | PG4 LOW — disable UV laser driver |
| `GANTRY <gcode>\n` | `OK\n` | Passthrough G-code to FluidNC |

### Pi TCP protocol (port 9876)

PC → Pi: `START_JOB <name>`, `PAUSE`, `RESUME`, `ABORT`, `LIST_JOBS`  
Pi → PC: `JOB_START <name>`, `FRAME n/total`, `GCODE <line>`, `JOB_DONE`, `JOB_PAUSED`, `JOBS job1,job2,...`, `ERROR <msg>`

## Work log

Current-state function docs for the Calibration Actions panel and camera
capture live in `Demo/source/LithoControl/README.md`. This section is for
root causes and why things were built the way they were — context that
doesn't belong in that README.

### 2026-08-26: Calibration workflow debugging session (branch `litho-rev`)

Long session getting the Calibration Actions panel actually working against
a real 1080p30 USB camera (Arducam IMX323, Amazon B0CGLW3Z1N) on an ME580
microscope rig, driven entirely by real `outputs.txt`/screenshot evidence
off the target Linux machine rather than guessed blind. In rough order:

- **`SIGCHLD` set to `SIG_IGN` process-wide** (`main.cpp`, meant only to
  reap the detached `LithoRevProjector` helper) was silently breaking
  `waitpid()` for every *other* subprocess in the app too, including
  `std::system()`-based checks like `Clipboard::commandExists("xclip")` —
  it kept reporting "not found" even though `apt` confirmed it was
  installed. Fixed by reaping only that specific helper's pid, from its
  own reader thread, instead of touching `SIGCHLD` disposition at all.
- **`runCapturedProcess()`'s `cd '<cwd>' && cmd 2>&1`** only redirected
  `cmd`'s stderr, not `cd`'s — a failed `cd` (e.g. a stale calib-dt folder
  path) went to this process's own inherited stderr (the real terminal)
  instead of into the pipe this function reads, invisible in the GUI's own
  runner log the whole time. Fixed by wrapping the whole list in `{ ...; }`.
- **Camera device list showed a real USB camera twice**, through three
  attempted fixes before the actual root cause: `scanCameras()`'s dedup key
  needs to be the device *name*, not label+path — every `/dev/videoN` path
  is unique by construction, so a path-based "exact duplicate" check can
  never catch a camera that legitimately exposes more than one
  `VIDEO_CAPTURE`-capable node.
- **A real segfault** turned out to be `applyCameraAdjustments(rgba, w, h)`
  mutating the capture loop's *persistent* w/h (reused every frame to index
  into the V4L2 mmap'd buffer) via its resize out-params — dormant for the
  entire life of that resize code, since the resolution preset defaulted to
  "Native" (0x0, resize path never ran) until this session changed that
  default. Fixed by passing local copies into that call instead.
- **Camera resolution presets were the AmScope MU130's native modes**
  (1280x1024/1024x768), a leftover from before the Linux port existed — not
  this camera at all. Went through 800x600 (this camera's actual raw-YUYV
  ceiling) before landing on real MJPEG support (`decodeToRGBAFromMemory`,
  `stb_image`, already vendored) to get genuine 1920x1080@30 — this camera
  needs MJPEG or H.264 for anything past ~800x600, a real USB2.0 bandwidth
  limit, not just a driver quirk.
- **`LITHOREV_DATA_DIR`**: camera captures/scale-reference frames were
  landing in `~/dev/Rev` even when launched off the USB drive, because
  `deploy-and-run.sh` always deploys-then-runs from `~/dev/Rev` regardless
  of whether it was invoked from the drive or the desktop shortcut — `cwd`
  alone can't tell those two launch styles apart. Needed an explicit env
  var from the deploy script, not a cwd guess. Same reasoning extended to
  calib-dt's own `--out` folders (`calibration_output` etc.), which are
  relative to `calibDtRoot` and were never mirrored to the drive at all
  until a user found a good calibration run sitting only on the target
  machine's disk with the USB drive already unplugged.
- **`SAVE SCALE REF` was capturing the wrong thing entirely**: the
  calibration slide's known-diameter circle isn't self-luminous, and a
  frame taken while the projector showed the calibration grid captured the
  grid reflecting off the slide, not the circle — confirmed from an actual
  shared image. Fixed by flashing solid white before capturing
  (`hdmiSolidColor` already takes priority over the grid in
  `composeHdmiFrame()`, so the grid itself needed no changes).
- **Projector helper process left running after closing via the OS "X"
  button**: `main()` calls `std::_Exit(0)` on exit (deliberately, to avoid a
  different hang from detached threads), which skips destructors entirely —
  nothing was telling `LithoRevProjector` to exit unless the in-app CLOSE
  button was used specifically. Fixed by calling `closeHdmiWindow()`
  (a synchronous kernel-level socket half-close, doesn't need this
  process to stay alive afterward) right before the `_Exit`.
- **A "successful" calibration run reporting `Final RMS: 0.0000 px`** was
  not evidence of a good fit — no `--holdout-fraction` was being passed, so
  the number was measured on the same points the model was fit on. Added a
  HOLDOUT FRACTION field. See the calib-dt repo's own `CLAUDE.md` for the
  matching entry on that side of this same investigation.
- **calib-dt's own captures were silently coming from the wrong camera** —
  reported independently, from outside this debugging session, by a
  teammate reviewing capture images ("your captures are all just your
  face! wrong camera"). Root cause: `capture-frames`/`run-calibration
  --camera` both open the camera in their own separate process from
  LithoRev's live preview, and neither call ever passed `--camera-index` —
  both default to camera 0 in calib-dt. On this machine (built-in webcam +
  USB microscope camera) that meant calib-dt's own captures could be from
  a completely different device than whatever the live preview correctly
  showed selected in the GUI, with nothing in the UI indicating a mismatch.
  Also a plausible contributor to the garbage grid-spacing/RMS numbers from
  the `0.0000 px` investigation above, independent of the holdout issue —
  fitting a projected-grid detector against a picture of someone's face
  isn't going to produce sane correspondences. Fixed by threading the live
  preview's selected camera index through to every calib-dt `--camera`
  call (`calibDtCameraIndexArg()`).
- **First real (`--camera-index` fixed) calibration run**: 24 genuine grid
  detections, no resolution-mismatch resample warning, and a real holdout
  score (16.79px) instead of a meaningless in-sample 0.0000 — a working
  baseline, though the measured grid spacing is still notably non-square
  (2x, down from ~3.4x on the wrong-camera run) and base RMS is still high
  (57.7px). Camera images shared during this run show visible tilt relative
  to the grid, which lines up with `diagnostic_report.txt`'s own
  "possible decenter, tilt" flag — not confirmed as the cause, but worth
  trying to straighten before assuming this asymmetry is unfixable/optical.
- **`CAPTURE FRAMES` timed out ("waiting for stable exposure")** when
  pressed with the projector idle — it never projected anything itself,
  just grabbed whatever was currently displayed (dark, in that case).
  Fixed the same way `RUN CALIBRATION` already handled its own grid
  capture: `captureFramesWithGrid()` projects the grid itself if it isn't
  already on, restoring prior state after.
- **Added `CAPTURE DELAY (s)`** (default 3s) — time between projecting
  the grid/white flash and actually capturing, with a per-second countdown
  in the runner log, so there's a deliberate window to step away from the
  microscope stage and let vibrations from touching it settle before a
  capture happens. Replaces the fixed 500ms waits `RUN CALIBRATION` and
  `SAVE SCALE REF` had been using.
- **`RUN CALIBRATION` failed with `"could not open camera index 2"`**
  moments after `CAPTURE FRAMES` had just used that same index
  successfully. Root cause: each calib-dt action (`CAPTURE FRAMES`,
  `RUN CALIBRATION`, `ANALYZE SENSITIVITY`) runs on its own independent
  detached thread with zero coordination between them. `outputs.txt`
  showed `CAPTURE FRAMES`'s tail end (mirroring output to the drive, then
  resuming the live camera preview) still executing several seconds
  *after* `RUN CALIBRATION` had already released the camera and launched
  its own calib-dt subprocess — the delayed `startCamera()` from the
  first action reopened the same device out from under the second one's
  `cv2.VideoCapture`, which then failed to open it at all. Fixed with a
  `calibActionRunning` guard in `runCalibDtSubprocess()`: a second action
  now refuses to start (clear status-banner message) while one is still
  in flight, rather than letting their camera-release/subprocess/camera-
  resume sequences interleave.
