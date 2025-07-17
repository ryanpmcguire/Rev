module;

#include <GLFW/glfw3.h>

export module Rev.CanvasOpenGL;

export namespace Rev {

    struct Canvas {

        struct Flags {
            bool resize = true;
            bool record = true;
        };

        GLFWwindow* window = nullptr;

        Flags flags;

        // Create
        Canvas(GLFWwindow* window = nullptr) {

            this->window = window;

            glfwMakeContextCurrent(window);
        }

        // Destroy
        ~Canvas() {
            
        }

        void draw() {
            if (!window) return;

            if (flags.resize) {
                int width, height;
                glfwGetFramebufferSize(window, &width, &height);
                glViewport(0, 0, width, height);
                flags.resize = false;
            }

            // Clear screen (customize as needed)
            glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            // Draw your OpenGL content here

            // Present
            glfwSwapBuffers(window);
        }
    };
};