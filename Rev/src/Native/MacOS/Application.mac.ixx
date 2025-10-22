module;

#include <vector>
#include <algorithm>

#include "NativeWindow.mac.h"

export module Rev.Application;

import Rev.Element.Window;

export namespace Rev {

    using namespace Rev::Element;

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
        
            while (!windows.empty()) {

                rev_mac_wait_event();
    
                // Cleanup closed windows
                for (auto it = windows.begin(); it != windows.end();) {

                    if ((*it)->shouldClose) {
                        delete *it; it = windows.erase(it);
                    }
                    
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
