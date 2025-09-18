module;

// Cpp
#include <stdexcept>
#include <functional>
#include <unordered_map>

#include <dbg.hpp>

export module Rev.NativeWindow;

export namespace Rev {

    struct NativeWindow {

        struct WinEvent {

            enum Type {
                Null,
                Create, Destroy,
                Focus, Defocus,
                Move, Resize, Maximize, Minimize, Restore,
                Clear, Paint,
                MouseButton, MouseMove,
                Keyboard, Character
            };

            // One type, four arbitrary data slots
            Type type;      // Event type
            uint64_t a, b;  // Unsigned data
            int64_t c, d;   // Signed data
        };

        enum ButtonAction {
            Release, Press, DoubleClick
        };

        enum MouseButton {
            Left, Right, Middle
        };

        // Keycode mapping
        //--------------------------------------------------
        
        // Unified key enum
        enum class Key {
            
            Unknown,

            Ctrl, Shift, Alt, Super,

            // Navigation
            Up, Down, Left, Right,
            PageUp, PageDown, Home, End, Insert, Delete,

            // Function keys
            F1, F2, F3, F4, F5, F6,
            F7, F8, F9, F10, F11, F12,

            // Numbers (top row)
            Num0, Num1, Num2, Num3, Num4,
            Num5, Num6, Num7, Num8, Num9,

            // Letters
            A, B, C, D, E, F, G, H, I, J,
            K, L, M, N, O, P, Q, R, S, T,
            U, V, W, X, Y, Z,

            // Numpad
            Numpad0, Numpad1, Numpad2, Numpad3, Numpad4,
            Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
            NumpadAdd, NumpadSub, NumpadMul, NumpadDiv, NumpadEnter, NumpadDecimal,

            // Misc
            Escape, Space, Tab, Enter, Backspace,
            CapsLock, NumLock, ScrollLock,
            PrintScreen, Pause,
        };

        // Win32 keycode -> key enum map
        inline static const std::unordered_map<int, Key> WinKeys = {

            
        };

        const char* keyToString(int key) { return keyToString(Key(key)); }
        const char* keyToString(Key key) {

            switch (key) {

                case Key::Ctrl:   return "Ctrl";
                case Key::Shift:  return "Shift";
                case Key::Alt:    return "Alt";
                case Key::Super:  return "Super";
        
                case Key::Up:    return "Up";
                case Key::Down:  return "Down";
                case Key::Left:  return "Left";
                case Key::Right: return "Right";
        
                case Key::PageUp:   return "PageUp";
                case Key::PageDown: return "PageDown";
                case Key::Home:     return "Home";
                case Key::End:      return "End";
                case Key::Insert:   return "Insert";
                case Key::Delete:   return "Delete";
        
                case Key::F1:  return "F1";
                case Key::F2:  return "F2";
                case Key::F3:  return "F3";
                case Key::F4:  return "F4";
                case Key::F5:  return "F5";
                case Key::F6:  return "F6";
                case Key::F7:  return "F7";
                case Key::F8:  return "F8";
                case Key::F9:  return "F9";
                case Key::F10: return "F10";
                case Key::F11: return "F11";
                case Key::F12: return "F12";
        
                case Key::A: return "A";
                case Key::B: return "B";
                case Key::C: return "C";
                // … repeat for all alphas/numbers …
        
                case Key::Escape:    return "Escape";
                case Key::Space:     return "Space";
                case Key::Tab:       return "Tab";
                case Key::Enter:     return "Enter";
                case Key::Backspace: return "Backspace";
        
                case Key::CapsLock:  return "CapsLock";
                case Key::NumLock:   return "NumLock";
                case Key::ScrollLock:return "ScrollLock";
        
                case Key::PrintScreen: return "PrintScreen";
                case Key::Pause:       return "Pause";
        
                default: return "Unknown";
            }
        }

        struct Size {
            int w, h, minW, minH, maxW, maxH;
        };

        using EventCallback = std::function<void(WinEvent)>;
        EventCallback callback;
        
        Size size;

        NativeWindow(void* parent, Size size = { 600, 400, 0, 0, 1000, 1000 }, EventCallback callback = nullptr) {
            
            this->size = size;
            this->callback = callback;

        }

        ~NativeWindow() {
        }

        void setSize(int w, int h) {
            
            this->size.w = w;
            this->size.h = h;
        
        }

        void notifyEvent(WinEvent event) {
            if (callback) { callback(event); }
        }
    

        

        // Getting OpenGL context
        //--------------------------------------------------

        void makeContextCurrent() {

            
        }

        void swapBuffers() {

        }
    };
};