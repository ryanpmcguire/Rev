module;

#include <stdexcept>
#include <windowsx.h>
#include <windows.h>
#include <dbg.hpp>

export module Rev.NativeWindow;

export namespace Rev {

    struct NativeWindow {

        struct Size {
            int w, h, minW, minH, maxW, maxH;
        };

        static constexpr LPCWSTR kClassName = L"Room360RawViewWindow";
        
        HWND hwnd = nullptr;

        Size size;

        NativeWindow(void* parent, Size size = { 600, 400, 0, 0, 1000, 1000 }) {
            
            this->size = size;

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
                    
                    return TRUE; // tell Windows creation may continue
                }

                case (WM_DESTROY): {
                    // Nothing special—host drives lifetime via IPlugView::removed()
                    return 0;
                }

                case (WM_SETFOCUS): { return 0; }   // Keyboard handling?
                case (WM_KILLFOCUS): { return 0; }

                case (WM_SIZE): {

                    dbg("WM_SIZE");

                    InvalidateRect(h, nullptr, FALSE);
                    return 0;
                }

                case (WM_ERASEBKGND): { return 1; }

                case (WM_PAINT): {

                    PAINTSTRUCT ps;
                    HDC dc = BeginPaint(h, &ps);
                    // Minimal background paint
                    FillRect(dc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));

                    // Example: draw a simple label so you can see it works
                    const wchar_t* text = L"Room360 RawView";
                    RECT r;
                    GetClientRect(h, &r);
                    DrawTextW(dc, text, -1, &r, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

                    EndPaint(h, &ps);
                    return 0;
                }

                // NEW: mouse moves
                case (WM_MOUSEMOVE): {
                    
                    dbg("WM_MOUSEMOVE");

                    return 0;
                }

                case WM_KEYDOWN: {
                    // wParam is the virtual key code (VK_*)
                    dbg("WM_KEYDOWN: VK=%d", (int)wp);
                
                    // Example: detect specific keys
                    if (wp == VK_SPACE) {
                        dbg("Space pressed!");
                    }
                    if (wp == 'A') {
                        dbg("A pressed!");
                    }
                
                    // Return 0 to mark as handled
                    return 0;
                }
                
                case WM_KEYUP: {
                    dbg("WM_KEYUP: VK=%d", (int)wp);
                
                    if (wp == 'A') {
                        dbg("A released!");
                    }
                
                    return 0;
                }
            }

            return DefWindowProcW(h, msg, wp, lp);
        }

        // Add to your struct:
        HDC   hdc   = nullptr;
        HGLRC hglrc = nullptr;

        // Local WGL typedefs (no loader needed)
        typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);
        typedef BOOL  (WINAPI *PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
        typedef BOOL  (WINAPI *PFNWGLSWAPINTERVALEXTPROC)(int);

        static ATOM registerDummyClass(HINSTANCE inst, LPCWSTR name, WNDPROC wndproc) {
            WNDCLASSW wc = {};
            wc.style         = CS_OWNDC;
            wc.lpfnWndProc   = wndproc;
            wc.hInstance     = inst;
            wc.lpszClassName = name;
            return RegisterClassW(&wc);
        }

        static LRESULT CALLBACK DummyWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
            return DefWindowProcW(h, m, w, l);
        }

        void makeContextCurrent() {
            if (!hwnd) throw std::runtime_error("[NativeWindow] No window handle available");

            HINSTANCE hinst = GetModuleHandleW(nullptr);

            // 1) DUMMY: class + window + legacy context (to load ARB funcs)
            const wchar_t* dummyClass = L"RevDummyGL";
            static bool dummyRegistered = false;
            if (!dummyRegistered) {
                if (!registerDummyClass(hinst, dummyClass, DummyWndProc))
                    throw std::runtime_error("[NativeWindow] Failed to register dummy GL class");
                dummyRegistered = true;
            }

            HWND dummy = CreateWindowW(dummyClass, L"", WS_OVERLAPPEDWINDOW,
                                    0, 0, 1, 1, nullptr, nullptr, hinst, nullptr);
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

            int pf = ChoosePixelFormat(dummyDC, &pfd);
            if (!pf || !SetPixelFormat(dummyDC, pf, &pfd)) {
                ReleaseDC(dummy, dummyDC);
                DestroyWindow(dummy);
                throw std::runtime_error("[NativeWindow] Dummy SetPixelFormat failed");
            }

            HGLRC dummyRC = wglCreateContext(dummyDC);
            if (!dummyRC) {
                ReleaseDC(dummy, dummyDC);
                DestroyWindow(dummy);
                throw std::runtime_error("[NativeWindow] Dummy wglCreateContext failed");
            }
            if (!wglMakeCurrent(dummyDC, dummyRC)) {
                wglDeleteContext(dummyRC);
                ReleaseDC(dummy, dummyDC);
                DestroyWindow(dummy);
                throw std::runtime_error("[NativeWindow] Dummy wglMakeCurrent failed");
            }

            // 2) Load ARB entry points
            auto wglCreateContextAttribsARB =
                (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
            auto wglChoosePixelFormatARB =
                (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
            auto wglSwapIntervalEXT =
                (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");

            if (!wglCreateContextAttribsARB || !wglChoosePixelFormatARB) {
                // Extensions not available – leave the dummy current so GLEW can still load,
                // OR fall back to legacy for the real window (but you'll stay on 2.1).
                // Here we error out to be explicit.
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
            if (!DescribePixelFormat(hdc, fmt, sizeof(chosenPFD), &chosenPFD) ||
                !SetPixelFormat(hdc, fmt, &chosenPFD)) {
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
                if (realRC) break;
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
            
        }
    };
};