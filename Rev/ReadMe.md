# Rev

GPU-accelerated C++23 GUI framework with a flex-style layout engine, CSS-like transitions, and native Win32/macOS windowing. No external UI dependencies — OpenGL on Windows/Linux, Metal on macOS.

## Architecture

```
Rev/                     Framework library (static lib)
├── src/
│   ├── Element/         Core element system
│   │   ├── Style/       Flex layout, transitions, Dist/Observable types
│   │   └── Event.ixx    Mouse, keyboard, focus events
│   ├── Elements/        Built-in widgets
│   │   ├── Box.ixx      Rectangular container (border, shadow, overflow clip)
│   │   ├── Text.ixx     Editable/selectable text (FreeType rendering)
│   │   ├── Svg.ixx      SVG display
│   │   └── Controls/
│   │       ├── Slider/       Value slider
│   │       ├── Dropdown/     Select menu
│   │       ├── Checkbox/     Boolean toggle
│   │       ├── Radio/        Option group
│   │       └── TextInput/    Labelled text field
│   ├── OS/
│   │   ├── Serial.ixx        Win32 serial port (read + write)
│   │   └── SocketClient.ixx  WinSock2 TCP client
│   ├── Graphics/        OpenGL/Metal canvas + primitives
│   ├── Native/          Win32 / macOS window creation
│   └── Window.ixx       Top-level window element
Demo/                    Demo application (executable)
└── source/
    ├── main.cpp
    ├── Interface.ixx    HelloWorld demo interface
    └── LithoControl/
        └── Interface.ixx  DLP photolithography control GUI
```

## Building

Requires: **CMake 3.26+**, **MSVC 2022** (C++23 modules), **Python 3** (resource embedder).

```
# From repo root — builds both Rev library and Demo exe
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --target HelloWorld
```

Output: `build/Demo/HelloWorld.exe`

On macOS, swap `--target HelloWorld` for the same target name; the Metal backend is selected automatically by CMake's platform filter (`.mac.ixx` files).

## Style system

Styles are plain C++ aggregate initialisers:

```cpp
Style panel = {
    .layout = { Axis::Vertical, Align::Start, Align::Start },
    .size   = { 320_px, 100_pct },
    .background = { .color = rgba(20, 20, 20, 1) },
    .border = { .radius = 4_px, .color = rgba(42, 42, 42, 1) },
    .shadow = { .color = rgba(0,0,0,0.5), .blur = 20_px }
};

// Conditional styles (hover, focus, press, drag, disabled)
Style hoverState = {
    .applies    = { .hover = true },
    .background = { .color = rgba(40, 40, 40, 1), .transition = 100_ms }
};
```

Pass styles to elements in the constructor: `new Box(parent, { &panel, &hoverState })`.  
Or assign inline: `box->style->background.color = rgba(0, 87, 255, 1)`.

## Element pattern

```cpp
struct MyWidget : public Box {
    Text* label = nullptr;

    MyWidget(Element* parent) : Box(parent) {
        this->style->layout = { Axis::Horizontal, Align::Center, Align::Center };
        this->style->size   = { Grow(), 40_px };

        label = new Text(this, "Hello");
        label->style->text.color = rgba(255, 255, 255, 1);

        this->onMouseDown([this](Event& e) {
            label->content = "Clicked!";
        });
    }

    void computeStyle(Event& e) override {
        // Runs every frame — safe to update element properties here
        Box::computeStyle(e);
    }
};
```

## OS modules

### `Rev::Serial` (`Rev.Serial`)

Win32 serial port. Opened at construction; auto-closed on destruction.

```cpp
Rev::Serial* ser = new Rev::Serial("COM3", 115200);
if (ser->connected()) {
    ser->sendText("PING\n");
    std::string resp = ser->readLine(3000);   // "PONG"
    auto data = ser->readBytes(28800, 10000); // 1bpp bitmap
}
```

| Method | Description |
|--------|-------------|
| `sendBytes(data, n)` | Write raw bytes |
| `sendText(str)` | Write string |
| `sendByte(b)` | Write one byte |
| `readLine(timeoutMs)` | Read until `\n`, strip `\r` |
| `readBytes(n, timeoutMs)` | Read exactly n bytes |
| `connected()` | Returns true if handle is valid |

### `Rev::SocketClient` (`Rev.SocketClient`)

WinSock2 TCP client. Background thread delivers complete lines via callback.

```cpp
Rev::SocketClient* client = new Rev::SocketClient([](Rev::SocketClient::Event ev) {
    if (ev.type == Rev::SocketClient::Event::Line) {
        // ev.data is one complete line (no \n)
    }
});

client->connect("192.168.1.240", 9876);
client->sendLine("START_JOB circle_test");
// ...
client->disconnect(); // or destructor handles it
```

| Event type | Meaning |
|------------|---------|
| `Connected` | TCP handshake complete |
| `Disconnected` | Remote closed or error |
| `Line` | One complete `\n`-terminated line received |
| `Error` | Connection or socket error (`ev.data` = message) |

## LithoControl demo

`Demo/source/LithoControl/Interface.ixx` is a full DLP photolithography control GUI for the [DLP-photolithography](https://github.com/DonMiller/DLP-photolithography) project. It replicates the Python `litho_gui.py` in native C++:

- **CONNECTION** panel — STM32 serial or Pi TCP, PING/PONG handshake
- **SLICER** panel — invokes `python pc/slicer.py` subprocess, captures stdout to log
- **JOB QUEUE** panel — local filesystem job browser, START / PAUSE / ABORT
- **JOG** panel — X/Y d-pad, HOME, E-STOP, RESET, SET HOME
- **Status** — frame progress bar, runner log, G-code stream log
- Settings persisted to `%APPDATA%\LithoControl\settings.ini`
- PNG frame → 1bpp bitmap via GDI+ (no stb_image dependency)
