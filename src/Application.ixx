module;

#include <vector>
#include <GLFW/glfw3.h>

export module Application;

import Window;
import WebGpu;

export namespace Rev {

    struct Application {

        std::vector<Window*> windows;
        WebGpu::Instance* webgpu;

        // Create
        Application() {

            glfwInit();

            webgpu = new WebGpu::Instance();
        }

        // Destroy
        ~Application() {
            
            delete webgpu;
            
            glfwTerminate();
        }

        void run() {

            while (true) {

                for (Window*& window : windows) {
                    if (window->shouldClose) { removeWindow(window); }
                }

                if (windows.empty()) { break; }

                glfwWaitEvents();
            }
        }

        // Remove window from our list
        void removeWindow(Window* target) {
            auto it = std::find(windows.begin(), windows.end(), target);
            if (it != windows.end()) { delete *it; windows.erase(it); }
        }
    };
}