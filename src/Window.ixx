module;

#include <vector>
#include <string>
#include <GLFW/glfw3.h>

export module Window;

export namespace Rev {

    struct Window {

        struct Details {

            int width = 640, height = 480;
            std::string name = "Hello World";

            bool resizable = true;
        };

        GLFWwindow* window = nullptr;
        Details details;
        bool shouldClose = false;

        // Create
        Window(std::vector<Window*>& group, Details details = {}) {

            this->details = details;

            // Create window
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_RESIZABLE, details.resizable ? GLFW_TRUE : GLFW_FALSE);
            window = glfwCreateWindow(details.width, details.height, details.name.c_str(), nullptr, nullptr);

            // Set user pointer and callbacks
            glfwSetWindowUserPointer(window, this);
            glfwSetWindowSizeCallback(window, handleResize);
            glfwSetWindowIconifyCallback(window, handleMinimize);
            glfwSetWindowCloseCallback(window, handleClose);

            group.push_back(this);
        }

        // Destroy
        ~Window() {
            glfwDestroyWindow(window);
        }

        virtual void onResize(float width, float height) {
            //shouldClose = true;
        }

        virtual void onMinimize() {
            
        }

        void onClose() {
            shouldClose = true;
        }

        static Window* self(GLFWwindow* win) {
            return static_cast<Window*>(glfwGetWindowUserPointer(win));
        }

        static void handleResize(GLFWwindow* win, int width, int height) { self(win)->onResize(float(width), float(height)); }
        static void handleMinimize(GLFWwindow* win, int minimized) { self(win)->onMinimize(); }
        static void handleClose(GLFWwindow* win) { self(win)->onClose(); }
    };
}