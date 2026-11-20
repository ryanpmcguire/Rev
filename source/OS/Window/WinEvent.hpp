#pragma once

struct WinEvent {

    enum Type {
        Null,
        Create, Destroy, Close,
        Focus, Defocus,
        Move, Resize, Maximize, Minimize, Restore,
        Scale, Clear, Paint,
        MouseButton, MouseMove, MouseWheel, CaptureLost,
        FileDrop,
        Keyboard, Character
    };

    Type type;
    uint64_t a, b;
    int64_t c, d;

    bool rejected = false;
    void* subject = nullptr;
    const void* data = nullptr; // FileDrop: const std::vector<std::wstring>* paths

    // Input events (mouse move/button/wheel, keyboard): when the OS generated
    // the input, in microseconds on a monotonic clock (Windows: QPC). 0 = unknown
    // (other events, or a platform that does not stamp yet).
    uint64_t inputUs = 0;

    void reject() {
        this->rejected = true;
    }
};