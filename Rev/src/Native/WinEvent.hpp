#pragma once

struct WinEvent {

    enum Type {
        Null,
        Create, Destroy, Close,
        Focus, Defocus,
        Move, Resize, Maximize, Minimize, Restore,
        Clear, Paint,
        MouseButton, MouseMove,
        Keyboard, Character
    };

    Type type;
    uint64_t a, b;
    int64_t c, d;

    bool rejected = false;

    void reject() {
        this->rejected = true;
    }
};