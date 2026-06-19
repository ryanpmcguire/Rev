# CLAUDE.md

This file provides guidance to Claude Code when working in the Rev repository.

## Project Overview

Rev is a GPU-accelerated C++23 GUI framework. It uses C++20 modules (`.ixx` files), a flex-style layout engine, and native platform backends (Win32 + OpenGL on Windows, Cocoa + Metal on macOS). The primary use case driving active development is a **DLP photolithography control GUI** (`Demo/source/LithoControl/Interface.ixx`).

## Build system

- **CMake 3.26+** required (C++ module scanning needs `CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API`)
- **MSVC 2022** for Windows; Apple Clang for macOS
- Source files are collected with `GLOB_RECURSE` — **adding a new `.ixx` file anywhere under `Rev/src/` or `Demo/source/` is automatically picked up, no CMakeLists edit needed** (exception: new link libraries must be added manually)
- Platform filter: files named `*.win.ixx` compile on Windows only; `*.mac.ixx` on macOS only

```
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target HelloWorld
```

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

### `Rev.Serial` (`Rev/src/OS/Serial.ixx`)

Win32 `CreateFile` serial port. Constructor opens port; destructor closes handle.

- `sendBytes(data, n)` — write raw bytes
- `sendText(str)` — write string
- `sendByte(b)` — write one byte
- `readLine(timeoutMs=5000)` — reads until `\n`, strips `\r`, returns `std::string`
- `readBytes(n, timeoutMs=5000)` — reads exactly n bytes, returns `std::vector<uint8_t>`
- `connected()` — true if handle is valid

**Timeout notes:** `readLine` / `readBytes` temporarily set `ReadTotalTimeoutConstant` to the given timeout (in ms), then restore the default (50ms). Always call from a background thread.

### `Rev.SocketClient` (`Rev/src/OS/SocketClient.ixx`)

WinSock2 TCP client. Connects synchronously with 5-second timeout; receives lines asynchronously on an internal thread.

- `connect(host, port)` — resolves hostname, connects, starts receive thread; returns `bool`
- `sendLine(msg)` — sends `msg + "\n"`
- `disconnect()` — shuts down socket, joins thread; safe to call from any thread
- `isConnected()` — true if socket is valid
- Callback fires on the receive thread — post to a `MsgQueue` rather than touching UI directly

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

`Demo/CMakeLists.txt` links `gdiplus comdlg32 shell32` in addition to `Rev`. These are Win32 system libraries — no install required.

### Key design decisions

- `pngToBitmap(path)` uses GDI+ (`Gdiplus::Bitmap::LockBits`) to decode PNG frames and pack them into 28,800-byte 1bpp MSB-first bitmaps for the STM32 `PATTERN` command
- Slicer runs as a subprocess (`CreateProcess` → `pc/slicer.py`) so the C++ GUI does not need Pillow/numpy
- Settings stored in `%APPDATA%\LithoControl\settings.ini` via `WritePrivateProfileString`
- `refreshJobList()` scans the local jobs directory for subdirs containing `manifest.json`
- The `selectedJob` string tracks the currently highlighted job across `rebuildJobList()` calls

### STM32 serial protocol (from `litho_runner.py` notes)

| Command | Response | Notes |
|---|---|---|
| `PING\n` | `PONG\n` | Connection health check |
| `PATTERN\n` | `READY\n` | Begin 28800-byte bitmap + 1-byte XOR checksum transfer |
| `[bitmap][checksum]` | `OK\n` | Bitmap received and checksummed |
| `EXPOSE <ms>\n` | `EXPOSING\n` → `DONE\n` / `ABORTED\n` | Expose loaded pattern |
| `BLANK\n` | (immediate) | Clear DMD |
| `FILL\n` | (immediate) | All pixels on |
| `GANTRY <gcode>\n` | `OK\n` | Passthrough G-code to FluidNC |

### Pi TCP protocol (port 9876)

PC → Pi: `START_JOB <name>`, `PAUSE`, `RESUME`, `ABORT`, `LIST_JOBS`  
Pi → PC: `JOB_START <name>`, `FRAME n/total`, `GCODE <line>`, `JOB_DONE`, `JOB_PAUSED`, `JOBS job1,job2,...`, `ERROR <msg>`
