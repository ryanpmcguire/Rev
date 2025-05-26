module;

#include <vector>
#include <string>
#include <GLFW/glfw3.h>

#define DEBUG true
#include <dbg.hpp>

export module Window;

import Element;

import WebGpu;

export namespace Rev {

    struct Window : public Element {

        struct Details {

            int width = 640, height = 480;
            std::string name = "Hello World";

            bool resizable = true;
        };

        // Glfw
        GLFWwindow* window = nullptr;
        Details details;

        bool shouldClose = false;

        // Create
        Window(std::vector<Window*>& group, Details details = {}) {

            this->details = details;

            // GLFW
            //--------------------------------------------------

            // Create window
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_RESIZABLE, details.resizable ? GLFW_TRUE : GLFW_FALSE);
            window = glfwCreateWindow(details.width, details.height, details.name.c_str(), nullptr, nullptr);

            // Set user pointer and callbacks
            glfwSetWindowUserPointer(window, this);
            glfwSetFramebufferSizeCallback(window, handleFramebufferResize);
            glfwSetWindowRefreshCallback(window, handleRefresh);
            glfwSetWindowContentScaleCallback(window, handleContentScale);
            glfwSetWindowFocusCallback(window, handleFocus);
            glfwSetWindowPosCallback(window, handleMove);
            glfwSetWindowSizeCallback(window, handleResize);
            glfwSetWindowIconifyCallback(window, handleMinimize);
            glfwSetWindowMaximizeCallback(window, handleMaximize);
            glfwSetWindowCloseCallback(window, handleClose);

            // WebGpu
            //--------------------------------------------------

            surface = new WebGpu::Surface(window);

            group.push_back(this);
        }

        // Destroy
        ~Window() {

            delete surface;

            glfwDestroyWindow(window);
        }

        // Draw
        //--------------------------------------------------

        void draw() {

            this->computeStyle();
            this->computePrimitives();
            surface->draw();
        }

        // Overridable callbacks
        //--------------------------------------------------

        // When the framebuffer resizes
        virtual void onFramebufferResize(int width, int height) {
            //dbg("Framebuffer: (%i, %i)", width, height);
        }

        // When the content needs to be redrawn
        virtual void onRefresh() {
            //dbg("Refresh");

            this->draw();
        }

        // When the content scale changes
        virtual void onContentScale(float xscale, float yscale) {
            //dbg("Content scale: (%f, %f)", xscale, yscale);
        }

        // When the window gains or loses focus
        virtual void onFocusChange(int focused) {
            if (focused) { this->onFocus(); }
            else { this->onDefocus(); }
        }

        // When the window gains focus
        virtual void onFocus() {
            //dbg("Focus");
        }

        // When the window loses focus
        virtual void onDefocus() {
            //dbg("Defocus");
        }

        // When the window changes position
        virtual void onMove(int x, int y) {
            //dbg("Move: (%i, %i)", x, y);
            this->draw();
        }

        // When the window is resized
        virtual void onResize(int width, int height) {
            //dbg("Resize: (%i, %i)", width, height);
            surface->flags.fit = true;
        }

        // When the window is maximized
        virtual void onMaximizeChange(int maximized) {
            if (maximized) { this->onMaximize(); }
            else { this->onDemaximize(); }
        }

        virtual void onMaximize() {
            //dbg("Maximize");
        }

        virtual void onDemaximize() {
            //dbg("Demaximize");
        }

        // When the window is minimized
        virtual void onMinimizeChange(int minimized) {
            if (minimized) { this->onMinimize(); }
            else { this->onDeminimize(); }
        }

        virtual void onMinimize() {
            //dbg("Minimize");
        }
        
        virtual void onDeminimize() {
            //dbg("Deminimize");
        }

        void onClose() {
            //dbg("Close");
            shouldClose = true;
        }

        // Static callbacks for GLFW
        //--------------------------------------------------

        static Window* self(GLFWwindow* win) {
            return static_cast<Window*>(glfwGetWindowUserPointer(win));
        }

        static void handleFramebufferResize(GLFWwindow* win, int width, int height) { self(win)->onFramebufferResize(width, height); }
        static void handleRefresh(GLFWwindow* win) { self(win)->onRefresh(); }
        static void handleContentScale(GLFWwindow* win, float xscale, float yscale) { self(win)->onContentScale(xscale, yscale);}
        static void handleFocus(GLFWwindow* win, int focused) { self(win)->onFocusChange(focused); }
        static void handleMove(GLFWwindow* win, int x, int y) { self(win)->onMove(x, y); }
        static void handleResize(GLFWwindow* win, int width, int height) { self(win)->onResize(width, height); }
        static void handleMaximize(GLFWwindow* win, int maximized) { self(win)->onMaximizeChange(maximized); }
        static void handleMinimize(GLFWwindow* win, int minimized) { self(win)->onMinimizeChange(minimized); }
        static void handleClose(GLFWwindow* win) { self(win)->onClose(); }
    };
}