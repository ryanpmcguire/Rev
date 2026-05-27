module;

#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <glew/glew.h>
#include <GL/glx.h>
#include <dbg.hpp>

#include "../WinEvent.hpp"

export module Rev.NativeWindow;

import Rev.Element.Style;

export namespace Rev {

    struct NativeWindow {

        enum ButtonAction { Release, Press, DoubleClick };
        enum MouseButton { Left, Right, Middle };

        enum class Key : int {
            Unknown,
            Ctrl, Shift, Alt, Super,
            Up, Down, Left, Right,
            PageUp, PageDown, Home, End, Insert, Delete,
            F1, F2, F3, F4, F5, F6,
            F7, F8, F9, F10, F11, F12,
            Num0, Num1, Num2, Num3, Num4,
            Num5, Num6, Num7, Num8, Num9,
            A, B, C, D, E, F, G, H, I, J,
            K, L, M, N, O, P, Q, R, S, T,
            U, V, W, X, Y, Z,
            Numpad0, Numpad1, Numpad2, Numpad3, Numpad4,
            Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
            NumpadAdd, NumpadSub, NumpadMul, NumpadDiv, NumpadEnter, NumpadDecimal,
            Escape, Space, Tab, Enter, Backspace,
            CapsLock, NumLock, ScrollLock,
            PrintScreen, Pause,
        };

        struct Display {
            int handle = 0;
            std::string friendlyName;
            int x, y, w, h;
            bool primary = false;
        };

        static std::vector<Display> getDisplays() {
            ensureDisplay();
            return {{
                .handle = screen,
                .friendlyName = "Default Display",
                .x = 0,
                .y = 0,
                .w = DisplayWidth(xDisplay, screen),
                .h = DisplayHeight(xDisplay, screen),
                .primary = true
            }};
        }

        struct Size { int w, h, minW, minH, maxW, maxH; };

        struct Details {
            Size size = { 640, 480, 0, 0, 1000, 1000 };
            bool decorated = true;
            bool resizable = true;
            bool borderless = false;
            bool fullscreen = false;
            bool closeButton = true;
            bool minimizeButton = true;
            bool maximizeButton = true;
        };

        enum class Relationship {
            Independent,
            OwnedTopLevel,
            EmbeddedChild
        };

        using EventCallback = std::function<void(WinEvent&)>;

        inline static ::Display* xDisplay = nullptr;
        inline static int screen = 0;
        inline static Atom wmDeleteWindow = 0;
        inline static std::unordered_map<::Window, NativeWindow*> windows;

        void* handle = nullptr;
        ::Window xWindow = 0;
        GLXContext glContext = nullptr;
        Colormap colormap = 0;
        XVisualInfo* visual = nullptr;
        EventCallback callback;

        Size size;
        float scale = 1.0f;
        Element::Cursor cursor;
        bool dirty = false;
        bool closed = false;
        Relationship relationship = Relationship::EmbeddedChild;

        NativeWindow(::Window parent, Size size = { 640, 480, 0, 0, 1000, 1000 }, EventCallback callback = nullptr)
            : NativeWindow(reinterpret_cast<void*>(static_cast<uintptr_t>(parent)), Details{ .size = size }, callback) {}

        NativeWindow(void* parent, Size size = { 640, 480, 0, 0, 1000, 1000 }, EventCallback callback = nullptr)
            : NativeWindow(parent, Details{ .size = size }, callback) {}

        NativeWindow(
            void* parent,
            Details details,
            EventCallback callback = nullptr,
            Relationship relationship = Relationship::EmbeddedChild
        ) {
            this->size = details.size;
            this->callback = callback;
            this->relationship = relationship;

            ensureDisplay();

            int attrs[] = {
                GLX_RGBA,
                GLX_DOUBLEBUFFER,
                GLX_DEPTH_SIZE, 24,
                GLX_STENCIL_SIZE, 8,
                GLX_RED_SIZE, 8,
                GLX_GREEN_SIZE, 8,
                GLX_BLUE_SIZE, 8,
                None
            };

            visual = glXChooseVisual(xDisplay, screen, attrs);
            if (!visual) throw std::runtime_error("[NativeWindow] glXChooseVisual failed");

            ::Window root = RootWindow(xDisplay, screen);
            colormap = XCreateColormap(xDisplay, root, visual->visual, AllocNone);

            XSetWindowAttributes swa{};
            swa.colormap = colormap;
            swa.event_mask = ExposureMask | StructureNotifyMask | FocusChangeMask |
                             PointerMotionMask | ButtonPressMask | ButtonReleaseMask |
                             KeyPressMask | KeyReleaseMask;

            ::Window parentWindow = parent ? static_cast<::Window>(reinterpret_cast<uintptr_t>(parent)) : root;

            xWindow = XCreateWindow(
                xDisplay,
                parentWindow,
                0, 0,
                static_cast<unsigned int>(size.w), static_cast<unsigned int>(size.h),
                0,
                visual->depth,
                InputOutput,
                visual->visual,
                CWColormap | CWEventMask,
                &swa
            );

            if (!xWindow) throw std::runtime_error("[NativeWindow] XCreateWindow failed");

            windows[xWindow] = this;
            XStoreName(xDisplay, xWindow, "Rev");
            XSetWMProtocols(xDisplay, xWindow, &wmDeleteWindow, 1);

            glContext = glXCreateContext(xDisplay, visual, nullptr, GL_TRUE);
            if (!glContext) throw std::runtime_error("[NativeWindow] glXCreateContext failed");

            XMapWindow(xDisplay, xWindow);
            XFlush(xDisplay);

            handle = reinterpret_cast<void*>(static_cast<uintptr_t>(xWindow));

            notifyEvent({ WinEvent::Type::Create });
            notifyEvent({ WinEvent::Type::Resize, 0, 0, size.w, size.h });
        }

        ~NativeWindow() {
            if (xDisplay && xWindow) {
                windows.erase(xWindow);
                if (glContext) {
                    if (glXGetCurrentContext() == glContext) glXMakeCurrent(xDisplay, None, nullptr);
                    glXDestroyContext(xDisplay, glContext);
                    glContext = nullptr;
                }
                XDestroyWindow(xDisplay, xWindow);
                xWindow = 0; handle = nullptr;
            }
            if (visual) XFree(visual);
            if (xDisplay && colormap) XFreeColormap(xDisplay, colormap);
        }

        const char* keyToString(int key) { return keyToString(Key(key)); }
        const char* keyToString(Key key) {
            switch (key) {
                case Key::Ctrl: return "Ctrl"; case Key::Shift: return "Shift";
                case Key::Alt: return "Alt"; case Key::Super: return "Super";
                case Key::Up: return "Up"; case Key::Down: return "Down";
                case Key::Left: return "Left"; case Key::Right: return "Right";
                case Key::Escape: return "Escape"; case Key::Space: return "Space";
                case Key::Tab: return "Tab"; case Key::Enter: return "Enter";
                case Key::Backspace: return "Backspace"; case Key::Delete: return "Delete";
                default: return "Unknown";
            }
        }

        void show() {
            XMapRaised(xDisplay, xWindow);
            XFlush(xDisplay);
        }

        void setPos(int x, int y) {
            XMoveWindow(xDisplay, xWindow, x, y);
        }

        void setTitle(const std::string& title) {
            XStoreName(xDisplay, xWindow, title.c_str());
        }

        void setRect(int x, int y, int w, int h) {
            size.w = w;
            size.h = h;
            XMoveResizeWindow(xDisplay, xWindow, x, y, static_cast<unsigned int>(w), static_cast<unsigned int>(h));
        }

        void setSize(int w, int h) {
            size.w = w;
            size.h = h;
            XResizeWindow(xDisplay, xWindow, static_cast<unsigned int>(w), static_cast<unsigned int>(h));
        }

        void setCursor(Element::Cursor newCursor) {
            cursor = newCursor;
        }

        void requestFrame() {
            if (dirty) return;
            dirty = true;
            XEvent ev{};
            ev.type = Expose;
            ev.xexpose.display = xDisplay;
            ev.xexpose.window = xWindow;
            XSendEvent(xDisplay, xWindow, False, ExposureMask, &ev);
            XFlush(xDisplay);
        }

        WinEvent notifyEvent(WinEvent event) {
            event.subject = this;
            if (callback) callback(event);
            return event;
        }

        struct ResourceContextGuard {
            NativeWindow* previous = nullptr;

            ResourceContextGuard(NativeWindow* window) {
                previous = currentWindow();
                if (window) window->makeContextCurrent();
            }

            ~ResourceContextGuard() {
                if (previous) previous->makeContextCurrent();
            }
        };

        static NativeWindow* currentWindow() {
            GLXContext current = glXGetCurrentContext();
            if (!current) return nullptr;
            for (auto& [_, window] : windows) {
                if (window && window->glContext == current) return window;
            }
            return nullptr;
        }

        static NativeWindow* requireContext(void* context, const char* operation = "OpenGL operation") {
            if (!context) throw std::runtime_error(std::string("[NativeWindow] Missing context for ") + operation);
            NativeWindow* window = static_cast<NativeWindow*>(context);
            window->makeContextCurrent();
            return window;
        }

        void createContext() {
            makeContextCurrent();
        }

        bool isContextCurrent() const {
            return glXGetCurrentContext() == glContext;
        }

        bool tryMakeContextCurrent() {
            return glXMakeCurrent(xDisplay, xWindow, glContext);
        }

        void makeContextCurrent() {
            if (!tryMakeContextCurrent()) {
                throw std::runtime_error("[NativeWindow] glXMakeCurrent failed");
            }
        }

        void loadGlFunctions() {
            glewExperimental = GL_TRUE;
            GLenum status = glewInit();
            glGetError();
            if (status != GLEW_OK) {
                throw std::runtime_error(reinterpret_cast<const char*>(glewGetErrorString(status)));
            }
            dbg("OpenGL INFO");
            dbg("GLEW version: %s", glewGetString(GLEW_VERSION));
            dbg("OpenGL version: %s", glGetString(GL_VERSION));
        }

        void swapBuffers() {
            glXSwapBuffers(xDisplay, xWindow);
            dirty = false;
        }

        static void ensureDisplay() {
            if (xDisplay) return;
            XInitThreads();
            xDisplay = XOpenDisplay(nullptr);
            if (!xDisplay) throw std::runtime_error("[NativeWindow] XOpenDisplay failed");
            screen = DefaultScreen(xDisplay);
            wmDeleteWindow = XInternAtom(xDisplay, "WM_DELETE_WINDOW", False);
        }

        static Key translateKey(KeySym sym) {
            switch (sym) {
                case XK_Control_L: case XK_Control_R: return Key::Ctrl;
                case XK_Shift_L: case XK_Shift_R: return Key::Shift;
                case XK_Alt_L: case XK_Alt_R: return Key::Alt;
                case XK_Super_L: case XK_Super_R: return Key::Super;
                case XK_Up: return Key::Up; case XK_Down: return Key::Down;
                case XK_Left: return Key::Left; case XK_Right: return Key::Right;
                case XK_Page_Up: return Key::PageUp; case XK_Page_Down: return Key::PageDown;
                case XK_Home: return Key::Home; case XK_End: return Key::End;
                case XK_Insert: return Key::Insert; case XK_Delete: return Key::Delete;
                case XK_F1: return Key::F1; case XK_F2: return Key::F2; case XK_F3: return Key::F3;
                case XK_F4: return Key::F4; case XK_F5: return Key::F5; case XK_F6: return Key::F6;
                case XK_F7: return Key::F7; case XK_F8: return Key::F8; case XK_F9: return Key::F9;
                case XK_F10: return Key::F10; case XK_F11: return Key::F11; case XK_F12: return Key::F12;
                case XK_Escape: return Key::Escape; case XK_space: return Key::Space;
                case XK_Tab: return Key::Tab; case XK_Return: return Key::Enter;
                case XK_BackSpace: return Key::Backspace;
                case XK_0: return Key::Num0; case XK_1: return Key::Num1; case XK_2: return Key::Num2;
                case XK_3: return Key::Num3; case XK_4: return Key::Num4; case XK_5: return Key::Num5;
                case XK_6: return Key::Num6; case XK_7: return Key::Num7; case XK_8: return Key::Num8;
                case XK_9: return Key::Num9;
                case XK_a: case XK_A: return Key::A; case XK_b: case XK_B: return Key::B;
                case XK_c: case XK_C: return Key::C; case XK_d: case XK_D: return Key::D;
                case XK_e: case XK_E: return Key::E; case XK_f: case XK_F: return Key::F;
                case XK_g: case XK_G: return Key::G; case XK_h: case XK_H: return Key::H;
                case XK_i: case XK_I: return Key::I; case XK_j: case XK_J: return Key::J;
                case XK_k: case XK_K: return Key::K; case XK_l: case XK_L: return Key::L;
                case XK_m: case XK_M: return Key::M; case XK_n: case XK_N: return Key::N;
                case XK_o: case XK_O: return Key::O; case XK_p: case XK_P: return Key::P;
                case XK_q: case XK_Q: return Key::Q; case XK_r: case XK_R: return Key::R;
                case XK_s: case XK_S: return Key::S; case XK_t: case XK_T: return Key::T;
                case XK_u: case XK_U: return Key::U; case XK_v: case XK_V: return Key::V;
                case XK_w: case XK_W: return Key::W; case XK_x: case XK_X: return Key::X;
                case XK_y: case XK_Y: return Key::Y; case XK_z: case XK_Z: return Key::Z;
                default: return Key::Unknown;
            }
        }

        static void pumpEvents() {
            ensureDisplay();
            while (XPending(xDisplay) > 0) {
                XEvent ev{};
                XNextEvent(xDisplay, &ev);

                NativeWindow* self = nullptr;
                auto it = windows.find(ev.xany.window);
                if (it != windows.end()) self = it->second;
                if (!self) continue;

                switch (ev.type) {
                    case ClientMessage:
                        if (static_cast<Atom>(ev.xclient.data.l[0]) == wmDeleteWindow) {
                            self->closed = true;
                            self->notifyEvent({ WinEvent::Type::Close });
                        }
                        break;
                    case DestroyNotify:
                        self->notifyEvent({ WinEvent::Type::Destroy });
                        break;
                    case FocusIn:
                        self->notifyEvent({ WinEvent::Type::Focus });
                        break;
                    case FocusOut:
                        self->notifyEvent({ WinEvent::Type::Defocus });
                        break;
                    case ConfigureNotify:
                        if (self->size.w != ev.xconfigure.width || self->size.h != ev.xconfigure.height) {
                            self->size.w = ev.xconfigure.width;
                            self->size.h = ev.xconfigure.height;
                            self->notifyEvent({ WinEvent::Type::Resize, 0, 0, self->size.w, self->size.h });
                        }
                        self->notifyEvent({ WinEvent::Type::Move, 0, 0, ev.xconfigure.x, ev.xconfigure.y });
                        break;
                    case Expose:
                        if (ev.xexpose.count == 0) self->notifyEvent({ WinEvent::Type::Paint });
                        break;
                    case MotionNotify:
                        self->notifyEvent({ WinEvent::Type::MouseMove, 0, 0, ev.xmotion.x, ev.xmotion.y });
                        break;
                    case ButtonPress:
                        if (ev.xbutton.button == Button4) self->notifyEvent({ WinEvent::Type::MouseWheel, 0, 0, 0, 1 });
                        else if (ev.xbutton.button == Button5) self->notifyEvent({ WinEvent::Type::MouseWheel, 0, 0, 0, -1 });
                        else self->notifyEvent({ WinEvent::Type::MouseButton, buttonFromX(ev.xbutton.button), Press, ev.xbutton.x, ev.xbutton.y });
                        break;
                    case ButtonRelease:
                        if (ev.xbutton.button != Button4 && ev.xbutton.button != Button5)
                            self->notifyEvent({ WinEvent::Type::MouseButton, buttonFromX(ev.xbutton.button), Release, ev.xbutton.x, ev.xbutton.y });
                        break;
                    case KeyPress: {
                        KeySym sym = XLookupKeysym(&ev.xkey, 0);
                        self->notifyEvent({ WinEvent::Type::Keyboard, static_cast<uint64_t>(translateKey(sym)), 1 });
                        char text[8]{};
                        KeySym ignored{};
                        int len = XLookupString(&ev.xkey, text, sizeof(text), &ignored, nullptr);
                        if (len > 0) self->notifyEvent({ WinEvent::Type::Character, static_cast<uint64_t>(static_cast<unsigned char>(text[0])) });
                        break;
                    }
                    case KeyRelease: {
                        KeySym sym = XLookupKeysym(&ev.xkey, 0);
                        self->notifyEvent({ WinEvent::Type::Keyboard, static_cast<uint64_t>(translateKey(sym)), 0 });
                        break;
                    }
                }
            }
        }

        static uint64_t buttonFromX(unsigned int button) {
            switch (button) {
                case Button1: return Left;
                case Button2: return Middle;
                case Button3: return Right;
                default: return Left;
            }
        }
    };
}
