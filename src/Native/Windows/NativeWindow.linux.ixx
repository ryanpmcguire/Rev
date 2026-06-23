module;

// Cpp
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// Linux / X11 / GLX
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <X11/Xutil.h>

// Misc
#if __has_include(<glew/glew.h>)
    #include <glew/glew.h>
#elif __has_include(<GL/glew.h>)
    #include <GL/glew.h>
#else
    #error "GLEW header not found. Install libglew-dev/glew-devel or adjust the include path."
#endif

#include <GL/glx.h>
#include <dbg.hpp>

#ifndef GLX_CONTEXT_MAJOR_VERSION_ARB
    #define GLX_CONTEXT_MAJOR_VERSION_ARB 0x2091
#endif
#ifndef GLX_CONTEXT_MINOR_VERSION_ARB
    #define GLX_CONTEXT_MINOR_VERSION_ARB 0x2092
#endif
#ifndef GLX_CONTEXT_PROFILE_MASK_ARB
    #define GLX_CONTEXT_PROFILE_MASK_ARB 0x9126
#endif
#ifndef GLX_CONTEXT_CORE_PROFILE_BIT_ARB
    #define GLX_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#endif

#include "../WinEvent.hpp"

export module Rev.NativeWindow;

import Rev.Appearance;

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

        inline static const std::unordered_map<KeySym, Key> XKeys = {
            { XK_Control_L, Key::Ctrl }, { XK_Control_R, Key::Ctrl },
            { XK_Shift_L, Key::Shift }, { XK_Shift_R, Key::Shift },
            { XK_Alt_L, Key::Alt }, { XK_Alt_R, Key::Alt },
            { XK_Meta_L, Key::Super }, { XK_Meta_R, Key::Super },
            { XK_Super_L, Key::Super }, { XK_Super_R, Key::Super },

            { XK_Up, Key::Up }, { XK_Down, Key::Down },
            { XK_Left, Key::Left }, { XK_Right, Key::Right },
            { XK_Page_Up, Key::PageUp }, { XK_Page_Down, Key::PageDown },
            { XK_Home, Key::Home }, { XK_End, Key::End },
            { XK_Insert, Key::Insert }, { XK_Delete, Key::Delete },

            { XK_F1, Key::F1 }, { XK_F2, Key::F2 }, { XK_F3, Key::F3 },
            { XK_F4, Key::F4 }, { XK_F5, Key::F5 }, { XK_F6, Key::F6 },
            { XK_F7, Key::F7 }, { XK_F8, Key::F8 }, { XK_F9, Key::F9 },
            { XK_F10, Key::F10 }, { XK_F11, Key::F11 }, { XK_F12, Key::F12 },

            { XK_0, Key::Num0 }, { XK_1, Key::Num1 }, { XK_2, Key::Num2 },
            { XK_3, Key::Num3 }, { XK_4, Key::Num4 }, { XK_5, Key::Num5 },
            { XK_6, Key::Num6 }, { XK_7, Key::Num7 }, { XK_8, Key::Num8 }, { XK_9, Key::Num9 },

            { XK_A, Key::A }, { XK_B, Key::B }, { XK_C, Key::C }, { XK_D, Key::D }, { XK_E, Key::E },
            { XK_F, Key::F }, { XK_G, Key::G }, { XK_H, Key::H }, { XK_I, Key::I }, { XK_J, Key::J },
            { XK_K, Key::K }, { XK_L, Key::L }, { XK_M, Key::M }, { XK_N, Key::N }, { XK_O, Key::O },
            { XK_P, Key::P }, { XK_Q, Key::Q }, { XK_R, Key::R }, { XK_S, Key::S }, { XK_T, Key::T },
            { XK_U, Key::U }, { XK_V, Key::V }, { XK_W, Key::W }, { XK_X, Key::X }, { XK_Y, Key::Y },
            { XK_Z, Key::Z },
            { XK_a, Key::A }, { XK_b, Key::B }, { XK_c, Key::C }, { XK_d, Key::D }, { XK_e, Key::E },
            { XK_f, Key::F }, { XK_g, Key::G }, { XK_h, Key::H }, { XK_i, Key::I }, { XK_j, Key::J },
            { XK_k, Key::K }, { XK_l, Key::L }, { XK_m, Key::M }, { XK_n, Key::N }, { XK_o, Key::O },
            { XK_p, Key::P }, { XK_q, Key::Q }, { XK_r, Key::R }, { XK_s, Key::S }, { XK_t, Key::T },
            { XK_u, Key::U }, { XK_v, Key::V }, { XK_w, Key::W }, { XK_x, Key::X }, { XK_y, Key::Y },
            { XK_z, Key::Z },

            { XK_KP_0, Key::Numpad0 }, { XK_KP_1, Key::Numpad1 }, { XK_KP_2, Key::Numpad2 },
            { XK_KP_3, Key::Numpad3 }, { XK_KP_4, Key::Numpad4 }, { XK_KP_5, Key::Numpad5 },
            { XK_KP_6, Key::Numpad6 }, { XK_KP_7, Key::Numpad7 }, { XK_KP_8, Key::Numpad8 },
            { XK_KP_9, Key::Numpad9 },
            { XK_KP_Add, Key::NumpadAdd }, { XK_KP_Subtract, Key::NumpadSub },
            { XK_KP_Multiply, Key::NumpadMul }, { XK_KP_Divide, Key::NumpadDiv },
            { XK_KP_Enter, Key::NumpadEnter }, { XK_KP_Decimal, Key::NumpadDecimal },

            { XK_Escape, Key::Escape }, { XK_space, Key::Space },
            { XK_Tab, Key::Tab }, { XK_Return, Key::Enter },
            { XK_BackSpace, Key::Backspace },
            { XK_Caps_Lock, Key::CapsLock }, { XK_Num_Lock, Key::NumLock },
            { XK_Scroll_Lock, Key::ScrollLock }, { XK_Print, Key::PrintScreen },
            { XK_Pause, Key::Pause },
        };

        const char* keyToString(int key) { return keyToString(Key(key)); }
        const char* keyToString(Key key) {
            switch (key) {
                case Key::Ctrl: return "Ctrl"; case Key::Shift: return "Shift";
                case Key::Alt: return "Alt"; case Key::Super: return "Super";
                case Key::Up: return "Up"; case Key::Down: return "Down";
                case Key::Left: return "Left"; case Key::Right: return "Right";
                case Key::PageUp: return "PageUp"; case Key::PageDown: return "PageDown";
                case Key::Home: return "Home"; case Key::End: return "End";
                case Key::Insert: return "Insert"; case Key::Delete: return "Delete";
                case Key::F1: return "F1"; case Key::F2: return "F2"; case Key::F3: return "F3";
                case Key::F4: return "F4"; case Key::F5: return "F5"; case Key::F6: return "F6";
                case Key::F7: return "F7"; case Key::F8: return "F8"; case Key::F9: return "F9";
                case Key::F10: return "F10"; case Key::F11: return "F11"; case Key::F12: return "F12";
                case Key::Num0: return "0"; case Key::Num1: return "1"; case Key::Num2: return "2";
                case Key::Num3: return "3"; case Key::Num4: return "4"; case Key::Num5: return "5";
                case Key::Num6: return "6"; case Key::Num7: return "7"; case Key::Num8: return "8";
                case Key::Num9: return "9";
                case Key::A: return "A"; case Key::B: return "B"; case Key::C: return "C";
                case Key::D: return "D"; case Key::E: return "E"; case Key::F: return "F";
                case Key::G: return "G"; case Key::H: return "H"; case Key::I: return "I";
                case Key::J: return "J"; case Key::K: return "K"; case Key::L: return "L";
                case Key::M: return "M"; case Key::N: return "N"; case Key::O: return "O";
                case Key::P: return "P"; case Key::Q: return "Q"; case Key::R: return "R";
                case Key::S: return "S"; case Key::T: return "T"; case Key::U: return "U";
                case Key::V: return "V"; case Key::W: return "W"; case Key::X: return "X";
                case Key::Y: return "Y"; case Key::Z: return "Z";
                case Key::Numpad0: return "Numpad0"; case Key::Numpad1: return "Numpad1";
                case Key::Numpad2: return "Numpad2"; case Key::Numpad3: return "Numpad3";
                case Key::Numpad4: return "Numpad4"; case Key::Numpad5: return "Numpad5";
                case Key::Numpad6: return "Numpad6"; case Key::Numpad7: return "Numpad7";
                case Key::Numpad8: return "Numpad8"; case Key::Numpad9: return "Numpad9";
                case Key::NumpadAdd: return "NumpadAdd"; case Key::NumpadSub: return "NumpadSub";
                case Key::NumpadMul: return "NumpadMul"; case Key::NumpadDiv: return "NumpadDiv";
                case Key::NumpadEnter: return "NumpadEnter"; case Key::NumpadDecimal: return "NumpadDecimal";
                case Key::Escape: return "Escape"; case Key::Space: return "Space";
                case Key::Tab: return "Tab"; case Key::Enter: return "Enter";
                case Key::Backspace: return "Backspace"; case Key::CapsLock: return "CapsLock";
                case Key::NumLock: return "NumLock"; case Key::ScrollLock: return "ScrollLock";
                case Key::PrintScreen: return "PrintScreen"; case Key::Pause: return "Pause";
                default: return "Unknown";
            }
        }

        struct Display {
            int handle = 0;
            std::string friendlyName;
            int x = 0, y = 0, w = 0, h = 0;
            bool primary = false;
        };

        static ::Display* openDisplay() {
            ::Display* d = XOpenDisplay(nullptr);
            if (!d) throw std::runtime_error("[NativeWindow] Failed to open X display");
            return d;
        }

        static std::vector<Display> getDisplays() {
            ::Display* d = openDisplay();
            int screen = DefaultScreen(d);
            std::vector<Display> displays;
            displays.push_back({
                .handle = screen,
                .friendlyName = "Default X11 Screen",
                .x = 0,
                .y = 0,
                .w = DisplayWidth(d, screen),
                .h = DisplayHeight(d, screen),
                .primary = true
            });
            XCloseDisplay(d);
            return displays;
        }

        struct Size { int w, h, minW, minH, maxW, maxH; };

        int posX = 0, posY = 0;

        using EventCallback = std::function<void(WinEvent&)>;
        EventCallback callback;

        inline static GLXContext sharedRoot = nullptr;
        inline static std::unordered_map<::Window, NativeWindow*> windows;

        ::Display* xDisplay = nullptr;
        int screen = 0;
        ::Window handle = 0;
        Atom wmDeleteWindow = None;
        XVisualInfo* visualInfo = nullptr;
        GLXFBConfig fbConfig = nullptr;
        Colormap colormap = 0;

        GLXContext hglrc = nullptr;

        Size size;
        float scale = 1.0f;
        Appearance::Cursor cursor = Appearance::Cursor::Unset;
        bool dirty = false;
        bool invalidatePending = false;

        NativeWindow(void* parent,
                     Size size = { 640, 480, 0, 0, 1000, 1000 },
                     bool borderless = false,
                     EventCallback callback = nullptr) {
            this->size = size;
            this->callback = callback;

            xDisplay = openDisplay();
            screen = DefaultScreen(xDisplay);

            int fbAttribs[] = {
                GLX_X_RENDERABLE, True,
                GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
                GLX_RENDER_TYPE, GLX_RGBA_BIT,
                GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
                GLX_RED_SIZE, 8,
                GLX_GREEN_SIZE, 8,
                GLX_BLUE_SIZE, 8,
                GLX_ALPHA_SIZE, 8,
                GLX_DEPTH_SIZE, 24,
                GLX_STENCIL_SIZE, 8,
                GLX_DOUBLEBUFFER, True,
                None
            };

            int fbCount = 0;
            GLXFBConfig* fbConfigs = glXChooseFBConfig(xDisplay, screen, fbAttribs, &fbCount);
            if (!fbConfigs || fbCount == 0) {
                throw std::runtime_error("[NativeWindow] glXChooseFBConfig failed");
            }

            fbConfig = fbConfigs[0];
            visualInfo = glXGetVisualFromFBConfig(xDisplay, fbConfig);
            if (!visualInfo) {
                XFree(fbConfigs);
                throw std::runtime_error("[NativeWindow] glXGetVisualFromFBConfig failed");
            }

            ::Window parentWindow = parent
                ? static_cast<::Window>(reinterpret_cast<std::uintptr_t>(parent))
                : RootWindow(xDisplay, visualInfo->screen);

            colormap = XCreateColormap(xDisplay, RootWindow(xDisplay, visualInfo->screen), visualInfo->visual, AllocNone);

            XSetWindowAttributes swa = {};
            swa.colormap = colormap;
            swa.background_pixmap = None;
            swa.border_pixel = 0;
            swa.event_mask = ExposureMask | StructureNotifyMask | KeyPressMask | KeyReleaseMask |
                             ButtonPressMask | ButtonReleaseMask | PointerMotionMask | FocusChangeMask;

            handle = XCreateWindow(
                xDisplay,
                parentWindow,
                0, 0,
                static_cast<unsigned int>(size.w),
                static_cast<unsigned int>(size.h),
                0,
                visualInfo->depth,
                InputOutput,
                visualInfo->visual,
                CWBorderPixel | CWColormap | CWEventMask,
                &swa
            );

            XFree(fbConfigs);

            if (!handle) {
                throw std::runtime_error("[NativeWindow] Failed to create X11 window");
            }

            windows[handle] = this;

            XStoreName(xDisplay, handle, "Room360 UI");
            wmDeleteWindow = XInternAtom(xDisplay, "WM_DELETE_WINDOW", False);
            XSetWMProtocols(xDisplay, handle, &wmDeleteWindow, 1);

            if (borderless) {
                struct MotifHints {
                    unsigned long flags;
                    unsigned long functions;
                    unsigned long decorations;
                    long inputMode;
                    unsigned long status;
                } hints = { 2, 0, 0, 0, 0 };

                Atom motif = XInternAtom(xDisplay, "_MOTIF_WM_HINTS", False);
                XChangeProperty(xDisplay, handle, motif, motif, 32, PropModeReplace,
                                reinterpret_cast<unsigned char*>(&hints), 5);
            }

            if (size.minW > 0 || size.minH > 0 || size.maxW > 0 || size.maxH > 0) {
                XSizeHints sizeHints = {};
                sizeHints.flags = 0;

                if (size.minW > 0 || size.minH > 0) {
                    sizeHints.flags |= PMinSize;
                    sizeHints.min_width = size.minW;
                    sizeHints.min_height = size.minH;
                }

                if (size.maxW > 0 || size.maxH > 0) {
                    sizeHints.flags |= PMaxSize;
                    sizeHints.max_width = size.maxW;
                    sizeHints.max_height = size.maxH;
                }

                XSetWMNormalHints(xDisplay, handle, &sizeHints);
            }

            XMapWindow(xDisplay, handle);
            XFlush(xDisplay);

            notifyEvent({ WinEvent::Type::Create });
            notifyEvent({ WinEvent::Type::Scale });
            notifyEvent({ WinEvent::Type::Resize, 0, 0, size.w, size.h });
        }

        ~NativeWindow() {
            if (xDisplay && hglrc) {
                glXMakeCurrent(xDisplay, None, nullptr);
                glXDestroyContext(xDisplay, hglrc);
                hglrc = nullptr;
            }

            if (xDisplay && handle) {
                windows.erase(handle);
                XDestroyWindow(xDisplay, handle);
                handle = 0;
            }

            if (xDisplay && colormap) {
                XFreeColormap(xDisplay, colormap);
                colormap = 0;
            }

            if (visualInfo) {
                XFree(visualInfo);
                visualInfo = nullptr;
            }

            if (xDisplay) {
                XCloseDisplay(xDisplay);
                xDisplay = nullptr;
            }
        }

        void setSize(int w, int h) {
            size.w = w; size.h = h;
            XResizeWindow(xDisplay, handle, static_cast<unsigned int>(w), static_cast<unsigned int>(h));
            XFlush(xDisplay);
        }

        void setPos(int x, int y) {
            posX = x; posY = y;
            XMoveWindow(xDisplay, handle, x, y);
            XFlush(xDisplay);
        }

        void setRect(int x, int y, int w, int h) {
            posX = x; posY = y;
            size.w = w; size.h = h;
            XMoveResizeWindow(xDisplay, handle, x, y, static_cast<unsigned int>(w), static_cast<unsigned int>(h));
            XFlush(xDisplay);
        }

        void setCursor(Appearance::Cursor newCursor) {
            if (!xDisplay || !handle || cursor == newCursor) return;
            cursor = newCursor;

            unsigned int cursorShape = XC_left_ptr;
            switch (newCursor) {
                case Appearance::Cursor::Unset:
                case Appearance::Cursor::Default:
                case Appearance::Cursor::Arrow: cursorShape = XC_left_ptr; break;
                case Appearance::Cursor::Caret: cursorShape = XC_xterm; break;
                case Appearance::Cursor::Crosshair: cursorShape = XC_crosshair; break;
                case Appearance::Cursor::Hand: cursorShape = XC_hand2; break;
                case Appearance::Cursor::NotAllowed: cursorShape = XC_pirate; break;
                case Appearance::Cursor::ArrowsHorizontal: cursorShape = XC_sb_h_double_arrow; break;
                case Appearance::Cursor::ArrowsVertical: cursorShape = XC_sb_v_double_arrow; break;
                case Appearance::Cursor::ArrowsDiagonalUp: cursorShape = XC_top_right_corner; break;
                case Appearance::Cursor::ArrowsDiagonalDown: cursorShape = XC_bottom_right_corner; break;
                case Appearance::Cursor::ArrowsOmni: cursorShape = XC_fleur; break;
                default: cursorShape = XC_left_ptr; break;
            }

            Cursor xCursor = XCreateFontCursor(xDisplay, cursorShape);
            XDefineCursor(xDisplay, handle, xCursor);
            XFreeCursor(xDisplay, xCursor);
            XFlush(xDisplay);
        }

        void requestFrame() {
            if (dirty) {
                invalidatePending = true;
                return;
            }
            dirty = true;
            XClearArea(xDisplay, handle, 0, 0, 0, 0, True);
            XFlush(xDisplay);
        }

        void flushPendingInvalidate() {
            if (!invalidatePending) { return; }
            invalidatePending = false;
            XClearArea(xDisplay, handle, 0, 0, 0, 0, True);
            XFlush(xDisplay);
        }

        WinEvent notifyEvent(WinEvent event) {
            event.subject = this;
            if (event.type == WinEvent::Type::MouseButton || event.type == WinEvent::Type::MouseMove) {
                event.c = static_cast<decltype(event.c)>((float)event.c / scale);
                event.d = static_cast<decltype(event.d)>((float)event.d / scale);
            }
            if (callback) callback(event);
            return event;
        }

        Key getKey(KeySym keysym) {
            auto it = XKeys.find(keysym);
            return (it != XKeys.end()) ? it->second : Key::Unknown;
        }

        static NativeWindow* Self(::Window window) {
            auto it = windows.find(window);
            return it == windows.end() ? nullptr : it->second;
        }

        static void pollEvents() {
            // Process events for every live NativeWindow. Call this once per frame from your main loop.
            std::vector<NativeWindow*> live;
            live.reserve(windows.size());
            for (auto& [_, win] : windows) live.push_back(win);

            for (NativeWindow* win : live) {
                if (!win || !win->xDisplay) continue;

                while (XPending(win->xDisplay) > 0) {
                    XEvent event;
                    XNextEvent(win->xDisplay, &event);
                    NativeWindow* self = Self(event.xany.window);
                    if (!self) self = win;
                    self->handleEvent(event);
                }
            }
        }

        void handleEvent(const XEvent& event) {
            switch (event.type) {
                case ClientMessage: {
                    if (static_cast<Atom>(event.xclient.data.l[0]) == wmDeleteWindow) {
                        WinEvent result = notifyEvent({ WinEvent::Type::Close });
                        if (!result.rejected) {
                            notifyEvent({ WinEvent::Type::Destroy });
                            windows.erase(handle);
                            XDestroyWindow(xDisplay, handle);
                            handle = 0;
                        }
                    }
                    break;
                }

                case DestroyNotify:
                    notifyEvent({ WinEvent::Type::Destroy });
                    break;

                case FocusIn:
                    notifyEvent({ WinEvent::Type::Focus });
                    break;

                case FocusOut:
                    notifyEvent({ WinEvent::Type::Defocus });
                    break;

                case ConfigureNotify: {
                    bool resized = (event.xconfigure.width != size.w || event.xconfigure.height != size.h);
                    bool moved = (event.xconfigure.x != posX || event.xconfigure.y != posY);

                    posX = event.xconfigure.x;
                    posY = event.xconfigure.y;
                    size.w = event.xconfigure.width;
                    size.h = event.xconfigure.height;

                    if (moved) {
                        notifyEvent({ WinEvent::Type::Move, 0, 0, posX, posY });
                    }
                    if (resized) {
                        notifyEvent({ WinEvent::Type::Resize, 0, 0, size.w, size.h });
                    }
                    break;
                }

                case Expose:
                    if (event.xexpose.count == 0) {
                        dirty = true;
                        notifyEvent({ WinEvent::Type::Paint });
                        dirty = false;
                        flushPendingInvalidate();
                    }
                    break;

                case MotionNotify:
                    notifyEvent({ WinEvent::Type::MouseMove, 0, 0, event.xmotion.x, event.xmotion.y });
                    break;

                case ButtonPress: {
                    int button = -1;
                    int action = 1;
                    int wheelX = 0;
                    int wheelY = 0;

                    switch (event.xbutton.button) {
                        case Button1: button = 0; break;
                        case Button3: button = 1; break;
                        case Button2: button = 2; break;
                        case Button4: wheelY = 120; break;
                        case Button5: wheelY = -120; break;
                        case 6: wheelX = -120; break;
                        case 7: wheelX = 120; break;
                    }

                    if (wheelX || wheelY) {
                        notifyEvent({ WinEvent::Type::MouseWheel, 0, 0, wheelX, wheelY });
                    } else if (button >= 0) {
                        notifyEvent({ WinEvent::Type::MouseButton, static_cast<uint64_t>(button), static_cast<uint64_t>(action), event.xbutton.x, event.xbutton.y });
                    }
                    break;
                }

                case ButtonRelease: {
                    int button = -1;
                    switch (event.xbutton.button) {
                        case Button1: button = 0; break;
                        case Button3: button = 1; break;
                        case Button2: button = 2; break;
                        default: break;
                    }
                    if (button >= 0) {
                        notifyEvent({ WinEvent::Type::MouseButton, static_cast<uint64_t>(button), 0, event.xbutton.x, event.xbutton.y });
                    }
                    break;
                }

                case KeyPress:
                case KeyRelease: {
                    KeySym keysym = XLookupKeysym(const_cast<XKeyEvent*>(&event.xkey), 0);
                    uint64_t action = (event.type == KeyPress) ? 1 : 0;
                    notifyEvent({ WinEvent::Type::Keyboard, static_cast<uint64_t>(getKey(keysym)), action });

                    if (event.type == KeyPress) {
                        char buffer[8] = {};
                        KeySym ignored = NoSymbol;
                        int len = XLookupString(const_cast<XKeyEvent*>(&event.xkey), buffer, sizeof(buffer), &ignored, nullptr);
                        if (len > 0) {
                            // This mirrors the Windows WM_CHAR path for ASCII/UTF-8 single-byte input.
                            notifyEvent({ WinEvent::Type::Character, static_cast<uint64_t>(static_cast<unsigned char>(buffer[0])) });
                        }
                    }
                    break;
                }

                default:
                    break;
            }
        }

        using PFNGLXCREATECONTEXTATTRIBSARBPROC = GLXContext (*)(::Display*, GLXFBConfig, GLXContext, Bool, const int*);
        using PFNGLXSWAPINTERVALEXTPROC = void (*)(::Display*, GLXDrawable, int);
        using PFNGLXSWAPINTERVALMESAPROC = int (*)(unsigned int);
        using PFNGLXSWAPINTERVALSGIPROC = int (*)(int);

        void loadGlFunctions() {
            glewExperimental = GL_TRUE;
            GLenum glewStatus = glewInit();
            if (glewStatus != GLEW_OK) {
                const GLubyte* errorStr = glewGetErrorString(glewStatus);
                throw std::runtime_error(std::string("[Canvas] Glew init failed: ") + reinterpret_cast<const char*>(errorStr));
            }

            dbg("");
            dbg("OpenGL INFO");
            dbg("--------------------\n");
            dbg("GLEW version: %s", glewGetString(GLEW_VERSION));
            dbg("OpenGL version: %s", glGetString(GL_VERSION));
            dbg("GLSL version: %s", glGetString(GL_SHADING_LANGUAGE_VERSION));
            dbg("Renderer: %s", glGetString(GL_RENDERER));
            dbg("Vendor: %s", glGetString(GL_VENDOR));
            dbg("");
        }

        void createContext() {
            if (!xDisplay || !handle) throw std::runtime_error("[NativeWindow] No X11 window available");

            int fbAttribs[] = {
                GLX_X_RENDERABLE, True,
                GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
                GLX_RENDER_TYPE, GLX_RGBA_BIT,
                GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
                GLX_RED_SIZE, 8,
                GLX_GREEN_SIZE, 8,
                GLX_BLUE_SIZE, 8,
                GLX_ALPHA_SIZE, 8,
                GLX_DEPTH_SIZE, 24,
                GLX_STENCIL_SIZE, 8,
                GLX_DOUBLEBUFFER, True,
                None
            };

            int fbCount = 0;
            GLXFBConfig* fbConfigs = nullptr;
            GLXFBConfig chosenFbConfig = fbConfig;

            if (!chosenFbConfig) {
                fbConfigs = glXChooseFBConfig(xDisplay, screen, fbAttribs, &fbCount);
                if (!fbConfigs || fbCount == 0) {
                    throw std::runtime_error("[NativeWindow] glXChooseFBConfig failed");
                }
                chosenFbConfig = fbConfigs[0];
            }

            auto glXCreateContextAttribsARB = reinterpret_cast<PFNGLXCREATECONTEXTATTRIBSARBPROC>(
                glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glXCreateContextAttribsARB"))
            );

            GLXContext share = sharedRoot;
            GLXContext ctx = nullptr;

            if (glXCreateContextAttribsARB) {
                const int versions[][2] = { {4,6},{4,5},{4,4},{4,3},{4,2},{4,1},{4,0},{3,3} };
                for (const auto& v : versions) {
                    int ctxAttribs[] = {
                        GLX_CONTEXT_MAJOR_VERSION_ARB, v[0],
                        GLX_CONTEXT_MINOR_VERSION_ARB, v[1],
                        GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                        None
                    };
                    ctx = glXCreateContextAttribsARB(xDisplay, chosenFbConfig, share, True, ctxAttribs);
                    if (ctx) break;
                }
            }

            if (!ctx) {
                ctx = glXCreateNewContext(xDisplay, chosenFbConfig, GLX_RGBA_TYPE, share, True);
            }

            if (fbConfigs) XFree(fbConfigs);

            if (!ctx) {
                throw std::runtime_error("[NativeWindow] Failed to create GLX context");
            }

            if (!sharedRoot) sharedRoot = ctx;

            if (!glXMakeCurrent(xDisplay, handle, ctx)) {
                glXDestroyContext(xDisplay, ctx);
                throw std::runtime_error("[NativeWindow] glXMakeCurrent failed");
            }

            hglrc = ctx;

            auto swapIntervalEXT = reinterpret_cast<PFNGLXSWAPINTERVALEXTPROC>(
                glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glXSwapIntervalEXT"))
            );
            auto swapIntervalMESA = reinterpret_cast<PFNGLXSWAPINTERVALMESAPROC>(
                glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glXSwapIntervalMESA"))
            );
            auto swapIntervalSGI = reinterpret_cast<PFNGLXSWAPINTERVALSGIPROC>(
                glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glXSwapIntervalSGI"))
            );

            if (swapIntervalEXT) swapIntervalEXT(xDisplay, handle, 0);
            else if (swapIntervalMESA) swapIntervalMESA(0);
            else if (swapIntervalSGI) swapIntervalSGI(0);
        }

        void makeContextCurrent() {
            if (!xDisplay || !handle || !hglrc) {
                throw std::runtime_error("[NativeWindow] Invalid GL context");
            }
            if (!glXMakeCurrent(xDisplay, handle, hglrc)) {
                throw std::runtime_error("[NativeWindow] glXMakeCurrent failed");
            }
        }

        void swapBuffers() {
            if (xDisplay && handle) {
                dirty = false;
                glXSwapBuffers(xDisplay, handle);
            }
        }

        void getFramebufferSize(int& fbW, int& fbH) const {
            if (!xDisplay || !handle) {
                fbW = fbH = 0;
                return;
            }

            XWindowAttributes attrs = {};
            XGetWindowAttributes(xDisplay, handle, &attrs);
            fbW = static_cast<int>(std::round(attrs.width * scale));
            fbH = static_cast<int>(std::round(attrs.height * scale));
        }
    };
};
