module;

#include <stdexcept>
#include <functional>
#include <windowsx.h>
#include <windows.h>
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
                Keyboard
            };

            // One type, four arbitrary data slots (int)
            Type type;
            int a, b, c, d;
        };

        struct Size {
            int w, h, minW, minH, maxW, maxH;
        };

        using EventCallback = std::function<void(WinEvent)>;

        static constexpr LPCWSTR kClassName = L"Room360RawViewWindow";        
        HWND hwnd = nullptr;
        EventCallback callback;
        
        Size size;

        NativeWindow(void* parent, Size size = { 600, 400, 0, 0, 1000, 1000 }, EventCallback callback = nullptr) {
            
            this->size = size;
            this->callback = callback;

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

            hwnd = CreateWindowExW(
                0, kClassName, L"Room360 UI",
                style,
                CW_USEDEFAULT, CW_USEDEFAULT, size.w, size.h,
                parentHwnd, nullptr, GetModuleHandle(nullptr),
                this
            );

            if (!hwnd) {
                throw std::runtime_error("[NativeWindow] Failed to create window");
            }
        }

        ~NativeWindow() {
            if (hwnd) {
                DestroyWindow(hwnd);
                hwnd = nullptr;
            }
        }

        void setSize(int w, int h) {
            
            this->size.w = w;
            this->size.h = h;
            
            SetWindowPos(hwnd, nullptr, 0, 0, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
        }

        void notifyEvent(WinEvent event) {
            if (callback) { callback(event); }
        }

        static NativeWindow* Self(HWND h) {
            return reinterpret_cast<NativeWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        }

        static LRESULT CALLBACK eventHandler(HWND h, UINT msg, WPARAM wp, LPARAM lp) {

            // Retrieve instance for all other messages
            NativeWindow* self = Self(h);

            switch (msg) {

                case (WM_NCCREATE): {

                    CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
                    NativeWindow* self = static_cast<NativeWindow*>(cs->lpCreateParams);

                    // Store own self pointer 
                    if (self) {
                        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
                        self->hwnd = h;
                    }

                    self->notifyEvent({
                        WinEvent::Type::Create
                    });
                    
                    return TRUE; // tell Windows creation may continue
                }

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
                        LOWORD(lp), HIWORD(lp)
                    });

                    switch (wp) {
                        case (SIZE_MINIMIZED): { self->notifyEvent({ WinEvent::Type::Minimize }); break; }
                        case (SIZE_MAXIMIZED): { self->notifyEvent({ WinEvent::Type::Maximize }); break; }
                        case (SIZE_RESTORED): { self->notifyEvent({ WinEvent::Type::Restore }); break; }
                    }
  
                    return 0;
                }

                case (WM_MOVE): {

                    self->notifyEvent({
                        WinEvent::Type::Move,
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

                // NEW: mouse moves
                case (WM_MOUSEMOVE): {
                
                    self->notifyEvent({
                        WinEvent::Type::MouseMove,
                        GET_X_LPARAM(lp),
                        GET_Y_LPARAM(lp)
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
                case (WM_KEYDOWN): { self->notifyEvent({ WinEvent::Type::Keyboard }); return 0; }
                case (WM_KEYUP): { self->notifyEvent({ WinEvent::Type::Keyboard }); return 0; }
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

        void makeContextCurrent() {

            if (!hwnd) { throw std::runtime_error("[NativeWindow] No window handle available"); }

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
            hdc = GetDC(hwnd);
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
                ReleaseDC(hwnd, hdc); hdc = nullptr;
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