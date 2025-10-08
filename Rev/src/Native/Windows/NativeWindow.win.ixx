module;

// Cpp
#include <stdexcept>
#include <functional>
#include <unordered_map>

// Win32
#include <windowsx.h>
#include <windows.h>
#include <shellscalingapi.h>

// Misc
#include <glew/glew.h>
#include <dbg.hpp>

#include "../WinEvent.hpp"

export module Rev.NativeWindow;

export namespace Rev {

    struct NativeWindow {

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

            { VK_CONTROL, Key::Ctrl },
            { VK_SHIFT,   Key::Shift },
            { VK_MENU,    Key::Alt },
            { VK_LWIN,    Key::Super }, { VK_RWIN, Key::Super },
        
            { VK_UP,    Key::Up }, { VK_DOWN,  Key::Down },
            { VK_LEFT,  Key::Left }, { VK_RIGHT, Key::Right },
        
            { VK_PRIOR, Key::PageUp }, { VK_NEXT,  Key::PageDown },
            { VK_HOME,  Key::Home }, { VK_END,   Key::End },
            { VK_INSERT,Key::Insert }, { VK_DELETE,Key::Delete },
        
            { VK_F1,  Key::F1 }, { VK_F2,  Key::F2 }, { VK_F3,  Key::F3 },
            { VK_F4,  Key::F4 }, { VK_F5,  Key::F5 }, { VK_F6,  Key::F6 },
            { VK_F7,  Key::F7 }, { VK_F8,  Key::F8 }, { VK_F9,  Key::F9 },
            { VK_F10, Key::F10 },{ VK_F11, Key::F11 },{ VK_F12, Key::F12 },
        
            { '0', Key::Num0 }, { '1', Key::Num1 }, { '2', Key::Num2 },
            { '3', Key::Num3 }, { '4', Key::Num4 }, { '5', Key::Num5 },
            { '6', Key::Num6 }, { '7', Key::Num7 }, { '8', Key::Num8 }, { '9', Key::Num9 },
        
            { 'A', Key::A }, { 'B', Key::B }, { 'C', Key::C }, { 'D', Key::D }, { 'E', Key::E },
            { 'F', Key::F }, { 'G', Key::G }, { 'H', Key::H }, { 'I', Key::I }, { 'J', Key::J },
            { 'K', Key::K }, { 'L', Key::L }, { 'M', Key::M }, { 'N', Key::N }, { 'O', Key::O },
            { 'P', Key::P }, { 'Q', Key::Q }, { 'R', Key::R }, { 'S', Key::S }, { 'T', Key::T },
            { 'U', Key::U }, { 'V', Key::V }, { 'W', Key::W }, { 'X', Key::X }, { 'Y', Key::Y },
            { 'Z', Key::Z },
        
            { VK_NUMPAD0, Key::Numpad0 }, { VK_NUMPAD1, Key::Numpad1 }, { VK_NUMPAD2, Key::Numpad2 },
            { VK_NUMPAD3, Key::Numpad3 }, { VK_NUMPAD4, Key::Numpad4 }, { VK_NUMPAD5, Key::Numpad5 },
            { VK_NUMPAD6, Key::Numpad6 }, { VK_NUMPAD7, Key::Numpad7 }, { VK_NUMPAD8, Key::Numpad8 },
            { VK_NUMPAD9, Key::Numpad9 },

            { VK_ADD, Key::NumpadAdd }, { VK_SUBTRACT, Key::NumpadSub },
            { VK_MULTIPLY, Key::NumpadMul }, { VK_DIVIDE, Key::NumpadDiv },
            { VK_RETURN, Key::NumpadEnter }, { VK_DECIMAL, Key::NumpadDecimal },
        
            { VK_ESCAPE,    Key::Escape },
            { VK_SPACE,     Key::Space },
            { VK_TAB,       Key::Tab },
            { VK_RETURN,    Key::Enter },
            { VK_BACK,      Key::Backspace },
            { VK_CAPITAL,   Key::CapsLock },
            { VK_NUMLOCK,   Key::NumLock },
            { VK_SCROLL,    Key::ScrollLock },
            { VK_SNAPSHOT,  Key::PrintScreen },
            { VK_PAUSE,     Key::Pause },
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

        using EventCallback = std::function<void(WinEvent&)>;

        static constexpr LPCWSTR kClassName = L"Room360RawViewWindow";        
        HWND handle = nullptr;
        EventCallback callback;
        
        Size size;
        float scale = 1.0f;

        NativeWindow(void* parent, Size size = { 600, 400, 0, 0, 1000, 1000 }, EventCallback callback = nullptr) {
            
            this->size = size;
            this->callback = callback;

            // --------------------------------------------------
            // Enable Per-Monitor DPI Awareness once per process
            // --------------------------------------------------
            static bool dpiAwareSet = false;
            if (!dpiAwareSet) {

                dpiAwareSet = true;

                // Prefer modern API if available (Win10+)
                if (SetProcessDpiAwarenessContext) {
                    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
                }
                
                else {
                    // Fallback (Win8.1)
                    HMODULE shcore = LoadLibraryW(L"Shcore.dll");
                    if (shcore) {
                        using SetProcDpiAwareFn = HRESULT(WINAPI*)(PROCESS_DPI_AWARENESS);
                        auto fn = reinterpret_cast<SetProcDpiAwareFn>(
                            GetProcAddress(shcore, "SetProcessDpiAwareness"));
                        if (fn) fn(PROCESS_PER_MONITOR_DPI_AWARE);
                        FreeLibrary(shcore);
                    } else {
                        SetProcessDPIAware(); // legacy fallback
                    }
                }
            }

            // Register window class
            //--------------------------------------------------

            WNDCLASSW wc = {
                .style          = CS_HREDRAW | CS_VREDRAW,
                .lpfnWndProc    = eventHandler,
                .hInstance      = GetModuleHandleW(nullptr),
                .hCursor        = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW),
                .hbrBackground  = (HBRUSH)(COLOR_WINDOW + 1),
                .lpszClassName  = kClassName
            };

            RegisterClassW(&wc);

            // Register window
            //--------------------------------------------------
            
            HWND parentHwnd = static_cast<HWND>(parent);

            // Determine style based off of parent HWDN, if present
            DWORD style = WS_VISIBLE;
            if (parentHwnd) { style |= WS_CHILD; }
            else { style |= WS_OVERLAPPEDWINDOW; }

            handle = CreateWindowExW(
                0, kClassName, L"Room360 UI",
                style,
                CW_USEDEFAULT, CW_USEDEFAULT, size.w, size.h,
                parentHwnd, nullptr, GetModuleHandle(nullptr),
                this
            );

            if (!handle) {
                throw std::runtime_error("[NativeWindow] Failed to create window");
            }
        }

        ~NativeWindow() {
            if (handle) {
                DestroyWindow(handle);
                handle = nullptr;
            }
        }

        void setSize(int w, int h) {
            
            this->size.w = w;
            this->size.h = h;
            
            SetWindowPos(handle, nullptr, 0, 0, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
        }

        WinEvent notifyEvent(WinEvent event) {
            if (callback) { callback(event); }
            return event;
        }
        
        // Translate Win32 keycode to unified Key enum
        Key getKey(WPARAM wp) {
            auto it = WinKeys.find(static_cast<int>(wp));
            return (it != WinKeys.end()) ? it->second : Key::Unknown;
        }

        // Get self (user pointer) from window handle
        static NativeWindow* Self(HWND h) {
            return reinterpret_cast<NativeWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        }

        // Static event handler function passed to the Win32 api - captures and sends events back to self
        static LRESULT CALLBACK eventHandler(HWND h, UINT msg, WPARAM wp, LPARAM lp) {

            // Retrieve instance for all other messages
            NativeWindow* self = Self(h);

            switch (msg) {

                // When the window is first created
                case (WM_NCCREATE): {

                    CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
                    NativeWindow* self = static_cast<NativeWindow*>(cs->lpCreateParams);

                    // Store own self pointer 
                    if (self) {
                        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
                        self->handle = h;
                    }

                    self->notifyEvent({
                        WinEvent::Type::Create
                    });
                    
                    return TRUE; // tell Windows creation may continue
                }

                case (WM_CLOSE): {

                    WinEvent result = self->notifyEvent({
                        WinEvent::Type::Close
                    });

                    if (result.rejected) {
                        return 0;
                    }

                    DestroyWindow(h);
                }

                // When the window is destroyed
                case (WM_DESTROY): {
                    
                    self->notifyEvent({
                        WinEvent::Type::Destroy
                    });
                    
                    return 0;
                }

                // When the window gains focus
                case (WM_SETFOCUS): {
                    
                    self->notifyEvent({
                        WinEvent::Type::Focus
                    });
                    
                    return 0;
                }

                // When the window loses focus
                case (WM_KILLFOCUS): {
                    
                    self->notifyEvent({
                        WinEvent::Type::Defocus
                    });
                    
                    return 0;
                }

                // When the window is resized
                case (WM_SIZE): {

                    InvalidateRect(h, nullptr, FALSE);

                    self->size.w = LOWORD(lp);
                    self->size.h = HIWORD(lp);

                    self->notifyEvent({
                        WinEvent::Type::Resize,
                        0, 0,
                        LOWORD(lp), HIWORD(lp)
                    });

                    switch (wp) {
                        case (SIZE_MINIMIZED): { self->notifyEvent({ WinEvent::Type::Minimize }); break; }
                        case (SIZE_MAXIMIZED): { self->notifyEvent({ WinEvent::Type::Maximize }); break; }
                        case (SIZE_RESTORED): { self->notifyEvent({ WinEvent::Type::Restore }); break; }
                    }
  
                    return 0;
                }

                case (WM_DPICHANGED): {

                    UINT dpiX = HIWORD(wp);
                    UINT dpiY = LOWORD(wp);

                    RECT* const suggestedRect = reinterpret_cast<RECT*>(lp);

                    SetWindowPos(
                        h, nullptr,
                        suggestedRect->left, suggestedRect->top,
                        suggestedRect->right - suggestedRect->left,
                        suggestedRect->bottom - suggestedRect->top,
                        SWP_NOZORDER | SWP_NOACTIVATE
                    );

                    float scaleX = dpiX / 96.0f;
                    float scaleY = dpiY / 96.0f;

                    self->scale = scaleX;

                    self->notifyEvent({
                        WinEvent::Type::Scale,
                        0, 0,
                        (int)(scaleX * 100), (int)(scaleY * 100)
                    });

                    return 0;
                }

                // When the window is moved
                case (WM_MOVE): {

                    self->notifyEvent({
                        WinEvent::Type::Move,
                        0, 0,
                        GET_X_LPARAM(lp),
                        GET_Y_LPARAM(lp)
                    });

                    return 0;
                }

                case (WM_ERASEBKGND): {
                    
                    self->notifyEvent({
                        WinEvent::Type::Clear
                    });

                    return 1;
                }

                case (WM_PAINT): {

                    PAINTSTRUCT ps;
                    HDC dc = BeginPaint(h, &ps);

                    self->notifyEvent({
                        WinEvent::Type::Paint
                    });

                    EndPaint(h, &ps);
                    
                    return 0;
                }

                // When the mouse moves
                case (WM_MOUSEMOVE): {
                
                    self->notifyEvent({
                        WinEvent::Type::MouseMove,
                        0, 0,
                        GET_X_LPARAM(lp),
                        GET_Y_LPARAM(lp)
                    });

                    return 0;
                }

                // Vertical wheel (mouse or touchpad)
                case WM_MOUSEWHEEL: {

                    self->notifyEvent({
                        WinEvent::Type::MouseWheel,
                        0, 0, 0, GET_WHEEL_DELTA_WPARAM(wp)
                    });

                    return 0;
                }

                // Horizontal wheel (mouse tilt or touchpad)
                case WM_MOUSEHWHEEL: {

                    self->notifyEvent({
                        WinEvent::Type::MouseWheel,
                        0, 0, GET_WHEEL_DELTA_WPARAM(wp), 0
                    });

                    return 0;
                }

                // Vertical wheel (trackpad specifically)
                case (WM_POINTERWHEEL): {

                    self->notifyEvent({
                        WinEvent::Type::MouseWheel,
                        0, 0, 0, GET_WHEEL_DELTA_WPARAM(wp)
                    });

                    return 0;
                }

                // Horizontal wheel (trackpad specifically)
                case (WM_POINTERHWHEEL): {

                    self->notifyEvent({
                        WinEvent::Type::MouseWheel,
                        0, 0, GET_WHEEL_DELTA_WPARAM(wp), 0
                    });

                    return 0;
                }

                // Mouse up
                case (WM_LBUTTONUP): { self->notifyEvent({ WinEvent::Type::MouseButton, 0, 0, GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }); return 0; }
                case (WM_RBUTTONUP): { self->notifyEvent({ WinEvent::Type::MouseButton, 1, 0, GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }); return 0; }
                case (WM_MBUTTONUP): { self->notifyEvent({ WinEvent::Type::MouseButton, 2, 0, GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }); return 0; }
                
                // Mouse down
                case (WM_LBUTTONDOWN): { self->notifyEvent({ WinEvent::Type::MouseButton, 1, 1, GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }); return 0; }
                case (WM_RBUTTONDOWN): { self->notifyEvent({ WinEvent::Type::MouseButton, 1, 1, GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }); return 0; }
                case (WM_MBUTTONDOWN): { self->notifyEvent({ WinEvent::Type::MouseButton, 2, 1, GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }); return 0; }
        
                // Mouse double click
                case (WM_LBUTTONDBLCLK): { self->notifyEvent({ WinEvent::Type::MouseButton, 0, 2, GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }); return 0; }
                case (WM_RBUTTONDBLCLK): { self->notifyEvent({ WinEvent::Type::MouseButton, 1, 2, GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }); return 0; }
                case (WM_MBUTTONDBLCLK): { self->notifyEvent({ WinEvent::Type::MouseButton, 2, 2, GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }); return 0; }
      
                // Keyboard
                case (WM_KEYDOWN): { self->notifyEvent({ WinEvent::Type::Keyboard, (uint64_t)self->getKey(wp), 1 }); return 0; }
                case (WM_KEYUP): { self->notifyEvent({ WinEvent::Type::Keyboard, (uint64_t)self->getKey(wp), 0 }); return 0; }
                case (WM_CHAR): { self->notifyEvent({ WinEvent::Type::Character, (uint64_t)(wp) }); return 0; }
            }

            // Must call default window proc first
            return DefWindowProcW(h, msg, wp, lp);
        }

        // Getting OpenGL context
        //--------------------------------------------------

        // Add to your struct:
        HDC   hdc   = nullptr;
        HGLRC hglrc = nullptr;

        // Local WGL typedefs (no loader needed)
        typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);
        typedef BOOL  (WINAPI *PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
        typedef BOOL  (WINAPI *PFNWGLSWAPINTERVALEXTPROC)(int);

        static ATOM registerDummyClass(HINSTANCE inst, LPCWSTR name, WNDPROC wndproc) {

            WNDCLASSW wc = {
                .style = CS_OWNDC,
                .lpfnWndProc = wndproc,
                .hInstance = inst,
                .lpszClassName = name
            };

            return RegisterClassW(&wc);
        }

        static LRESULT CALLBACK DummyWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
            return DefWindowProcW(h, m, w, l);
        }

        void loadGlFunctions() {

            // 2. Initialize GLEW
            glewExperimental = GL_TRUE; // Enable core profiles
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

        void makeContextCurrent() {

            if (!handle) { throw std::runtime_error("[NativeWindow] No window handle available"); }

            HINSTANCE hinst = GetModuleHandleW(nullptr);

            // 1) DUMMY: class + window + legacy context (to load ARB funcs)
            const wchar_t* dummyClass = L"RevDummyGL";
            static bool dummyRegistered = false;

            if (!dummyRegistered) {

                if (!registerDummyClass(hinst, dummyClass, DummyWndProc))
                   { throw std::runtime_error("[NativeWindow] Failed to register dummy GL class"); }

                dummyRegistered = true;
            }

            HWND dummy = CreateWindowW(dummyClass, L"", WS_OVERLAPPEDWINDOW, 0, 0, 1, 1, nullptr, nullptr, hinst, nullptr);
            if (!dummy) throw std::runtime_error("[NativeWindow] Failed to create dummy window");

            HDC dummyDC = GetDC(dummy);

            // Basic legacy PFD (good enough to create a legacy context)
            PIXELFORMATDESCRIPTOR pfd = {
                sizeof(PIXELFORMATDESCRIPTOR), 1,
                (WORD)(PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER),
                PFD_TYPE_RGBA, 32,
                0,0,0,0,0,0,
                0, 0, 0, 0,0,0,0,
                24, 8, 0, PFD_MAIN_PLANE, 0, 0,0,0
            };

            // Choose pixel format
            int pf = ChoosePixelFormat(dummyDC, &pfd);
            if (!pf || !SetPixelFormat(dummyDC, pf, &pfd)) {

                ReleaseDC(dummy, dummyDC);
                DestroyWindow(dummy);

                throw std::runtime_error("[NativeWindow] Dummy SetPixelFormat failed");
            }

            // Create dummy context
            HGLRC dummyRC = wglCreateContext(dummyDC);
            if (!dummyRC) {
                ReleaseDC(dummy, dummyDC);
                DestroyWindow(dummy);
                throw std::runtime_error("[NativeWindow] Dummy wglCreateContext failed");
            }

            // Make dummy context current
            if (!wglMakeCurrent(dummyDC, dummyRC)) {
                wglDeleteContext(dummyRC);
                ReleaseDC(dummy, dummyDC);
                DestroyWindow(dummy);
                throw std::runtime_error("[NativeWindow] Dummy wglMakeCurrent failed");
            }

            // 2) Load ARB entry points
            auto wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
            auto wglChoosePixelFormatARB = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
            auto wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");

            // Extensions not available – leave the dummy current so GLEW can still load,
            // OR fall back to legacy for the real window (but you'll stay on 2.1).
            // Here we error out to be explicit.
            if (!wglCreateContextAttribsARB || !wglChoosePixelFormatARB) {

                wglMakeCurrent(nullptr, nullptr);
                wglDeleteContext(dummyRC);
                ReleaseDC(dummy, dummyDC);
                DestroyWindow(dummy);

                throw std::runtime_error("[NativeWindow] Required WGL ARB extensions not available");
            }

            // 3) Pick modern pixel format for the REAL window using wglChoosePixelFormatARB
            hdc = GetDC(handle);
            if (!hdc) {

                wglMakeCurrent(nullptr, nullptr);
                wglDeleteContext(dummyRC);
                ReleaseDC(dummy, dummyDC);
                DestroyWindow(dummy);

                throw std::runtime_error("[NativeWindow] GetDC failed for real window");
            }

            // Attribute lists (request sRGB/multisample if you like)
            int pixAttribs[] = {
                0x2001 /*WGL_DRAW_TO_WINDOW_ARB*/, TRUE,
                0x2010 /*WGL_SUPPORT_OPENGL_ARB*/, TRUE,
                0x2011 /*WGL_DOUBLE_BUFFER_ARB*/, TRUE,
                0x2013 /*WGL_PIXEL_TYPE_ARB*/,     0x202B /*WGL_TYPE_RGBA_ARB*/,
                0x2014 /*WGL_COLOR_BITS_ARB*/,     32,
                0x2022 /*WGL_DEPTH_BITS_ARB*/,     24,
                0x2023 /*WGL_STENCIL_BITS_ARB*/,   8,
                // Optional nice-to-haves:
                // 0x20A9 /*WGL_ACCELERATION_ARB*/,   0x2027 /*WGL_FULL_ACCELERATION_ARB*/,
                // 0x2041 /*WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB*/, TRUE,
                0, 0
            };

            int fmt = 0; UINT fmtCount = 0;
            if (!wglChoosePixelFormatARB(hdc, pixAttribs, nullptr, 1, &fmt, &fmtCount) || fmtCount == 0) {

                wglMakeCurrent(nullptr, nullptr);
                wglDeleteContext(dummyRC);
                ReleaseDC(dummy, dummyDC);
                DestroyWindow(dummy);

                throw std::runtime_error("[NativeWindow] wglChoosePixelFormatARB failed");
            }

            // You must still call SetPixelFormat once on the real DC (with a legacy PFD; the ARB choice wins)
            PIXELFORMATDESCRIPTOR chosenPFD;
            if (!DescribePixelFormat(hdc, fmt, sizeof(chosenPFD), &chosenPFD) || !SetPixelFormat(hdc, fmt, &chosenPFD)) {
                
                wglMakeCurrent(nullptr, nullptr);
                wglDeleteContext(dummyRC);
                ReleaseDC(dummy, dummyDC);
                DestroyWindow(dummy);
                
                throw std::runtime_error("[NativeWindow] SetPixelFormat (real) failed");
            }

            // 4) Create a modern Core profile context (try 4.6, fall back to 4.5, 4.4, ... 3.3)
            const int versions[][2] = { {4,6},{4,5},{4,4},{4,3},{4,2},{4,1},{4,0},{3,3} };
            HGLRC realRC = nullptr;

            for (auto& v : versions) {

                int ctxAttribs[] = {
                    0x2091 /*WGL_CONTEXT_MAJOR_VERSION_ARB*/, v[0],
                    0x2092 /*WGL_CONTEXT_MINOR_VERSION_ARB*/, v[1],
                    0x9126 /*WGL_CONTEXT_PROFILE_MASK_ARB*/,  0x00000001 /*WGL_CONTEXT_CORE_PROFILE_BIT_ARB*/,
                    // Optional debug:
                    // 0x2094 /*WGL_CONTEXT_FLAGS_ARB*/, 0x0001 /*WGL_CONTEXT_DEBUG_BIT_ARB*/,
                    0
                };

                realRC = wglCreateContextAttribsARB(hdc, 0, ctxAttribs);
                if (realRC) { break; }
            }

            if (!realRC) {

                wglMakeCurrent(nullptr, nullptr);
                wglDeleteContext(dummyRC);
                ReleaseDC(dummy, dummyDC);
                DestroyWindow(dummy);

                throw std::runtime_error("[NativeWindow] Failed to create modern GL context");
            }

            // 5) Activate the real context
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(dummyRC);
            ReleaseDC(dummy, dummyDC);
            DestroyWindow(dummy);

            if (!wglMakeCurrent(hdc, realRC)) {
                wglDeleteContext(realRC);
                ReleaseDC(handle, hdc); hdc = nullptr;
                throw std::runtime_error("[NativeWindow] wglMakeCurrent (real) failed");
            }

            hglrc = realRC;

            // Optional: enable vsync if extension is present
            if (wglSwapIntervalEXT) {
                wglSwapIntervalEXT(1); // 1 = vsync on, 0 = off
            }
        }

        void swapBuffers() {
            if (hdc) {
                SwapBuffers(hdc);
            }
        }
    };
};