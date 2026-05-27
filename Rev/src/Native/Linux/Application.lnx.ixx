module;

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

export module Rev.Application;

import Rev.Window;
import Rev.NativeWindow;

export namespace Rev {

    struct Application {

        std::vector<Window*> windows;

        Application() {}
        ~Application() {}

        void run() {
            while (!windows.empty()) {
                NativeWindow::pumpEvents();

                for (auto it = windows.begin(); it != windows.end();) {
                    if ((*it)->shouldClose || ((*it)->window && (*it)->window->closed)) {
                        delete *it;
                        it = windows.erase(it);
                    }
                    else {
                        ++it;
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        void removeWindow(Window* target) {
            auto it = std::find(windows.begin(), windows.end(), target);
            if (it != windows.end()) {
                delete *it;
                windows.erase(it);
            }
        }
    };
}
