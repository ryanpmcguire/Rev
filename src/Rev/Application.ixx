module;

#include <vector>
#include <GLFW/glfw3.h>

export module Rev.Application;

//import Vulkan.Instance;
import Rev.Window;

export namespace Rev {

    struct Application {

        //Vulkan::Instance* vulkan = nullptr;
        std::vector<Window*> windows;

        // Create
        Application() {
            glfwInit();
        }

        // Destroy
        ~Application() {
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