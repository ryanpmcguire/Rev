module;

#include <vector>
#include <windows.h>   // Win32 API
#include <algorithm>

export module Rev.Application;

import Rev.Window;

export namespace Rev {

    struct Application {

        std::vector<Window*> windows;

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
                    for (Window* w : windows) { delete w; }
                    windows.clear();
                    return;
                }

                TranslateMessage(&msg);
                DispatchMessage(&msg);
    
                // Cleanup closed windows
                for (auto it = windows.begin(); it != windows.end();) {
                    if ((*it)->shouldClose) { delete *it; it = windows.erase(it); }
                    else { ++it; }
                }
            }
        }

        // Remove window from our list
        void removeWindow(Window* target) {

            auto it = std::find(windows.begin(), windows.end(), target);
            
            if (it != windows.end()) {
                delete *it;
                windows.erase(it);
            }
        }
    };
}
