module;

#define UNICODE
#define _UNICODE
#include <windows.h>

export module Rev.Window;

export namespace Rev {
 
    struct Window {

        static constexpr const wchar_t* CLASS_NAME = L"RevWindowClass";

        // Member variables
        HINSTANCE hInstance = nullptr;
        HWND hwnd = nullptr;

        Window() {

            hInstance = GetModuleHandle(nullptr);

            WNDCLASS wc = {};
            wc.lpfnWndProc = WindowProc;
            wc.hInstance = hInstance;
            wc.lpszClassName = CLASS_NAME;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

            RegisterClass(&wc);

            hwnd = CreateWindowEx(
                0,
                CLASS_NAME,
                L"Hello World",
                WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
                nullptr,
                nullptr,
                hInstance,
                nullptr
            );

            if (hwnd) {
                ShowWindow(hwnd, SW_SHOW);
            } else {
                MessageBox(nullptr, L"Window creation failed", L"Error", MB_OK | MB_ICONERROR);
            }
        }

        ~Window() {
            if (hwnd) {
                DestroyWindow(hwnd);
            }
        }

        void run() {

            MSG msg = {};

            while (GetMessage(&msg, nullptr, 0, 0)) {

                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {

            switch (uMsg) {

            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                EndPaint(hwnd, &ps);
                return 0;
            }
            
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            }
    
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
    };
}