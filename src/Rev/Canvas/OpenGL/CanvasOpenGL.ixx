module;

#include <glew/glew.h>
#include <GLFW/glfw3.h>
#include <dbg.hpp>

export module Rev.CanvasOpenGL;

import Rev.OpenGL.Triangles;

export namespace Rev {

    struct Canvas {

        struct Flags {
            bool resize = true;
            bool record = true;
        };

        GLFWwindow* window = nullptr;

        Flags flags;

        Triangles* triangles = nullptr;

        // Create
        Canvas(GLFWwindow* window = nullptr) {

            this->window = window;

            glfwMakeContextCurrent(window);

            // 2. Initialize GLEW
            glewExperimental = GL_TRUE; // Enable core profiles
            GLenum glewStatus = glewInit();

            if (glewStatus != GLEW_OK) {
                const GLubyte* errorStr = glewGetErrorString(glewStatus);
                dbg("GLEW init failed: %s\n", errorStr);
                //std::exit(1); // Optional, but recommended to bail
            }

            triangles = new Triangles();
        }

        // Destroy
        ~Canvas() {
            
            delete triangles;
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
            triangles->draw();

            // Present
            glfwSwapBuffers(window);
        }
    };
};