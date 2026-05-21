module;

#include <vector>
#include <windows.h>   // Win32 API
#include <algorithm>

export module Rev.Application;

import Rev.Window;

export namespace Rev {

    struct Application {

        std::vector<void*> windows;

        // Create
        Application() {
            // Win32 needs an HINSTANCE, but window creation
            // will handle RegisterClass etc. at that layer.
        }

        // Destroy
        ~Application() {
            // Nothing to do here yet, unless we global-cleanup something.
        }

        void run() {

            MSG msg = {0};

            while (!windows.empty()) {

                // This blocks until a message arrives
                BOOL result = GetMessage(&msg, nullptr, 0, 0);
                if (!result) { break; }

                if (msg.message == WM_QUIT) {
                    for (void* handle : windows) {
                        delete static_cast<Window*>(handle);
                    }
                    windows.clear();
                    return;
                }

                TranslateMessage(&msg);
                DispatchMessage(&msg);
    
                // Cleanup closed windows (erase before delete so ~Window
                // does not mutate the vector under this iterator).
                for (auto it = windows.begin(); it != windows.end();) {
                    Window* w = static_cast<Window*>(*it);

                    if (w->shouldClose) {
                        it = windows.erase(it);
                        delete w;
                    }

                    else { ++it; }
                }
            }
        }

        // Remove window from our list
        void removeWindow(Window* target) {

            void* handle = static_cast<void*>(target);

            auto it = std::find(windows.begin(), windows.end(), handle);
            
            if (it != windows.end()) {
                it = windows.erase(it);
                delete target;
            }
        }
    };
}
