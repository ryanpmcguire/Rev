module;

#include <stdexcept>
#include <vector>
#include <glew/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <dbg.hpp>

export module Rev.OpenGL.Canvas;

import Rev.NativeWindow;
import Rev.OpenGL.UniformBuffer;

export namespace Rev {

    struct Canvas {

        struct Flags {
            bool resize = true;
            bool record = true;
        };

        struct Details {
            int width, height;
        };

        NativeWindow* window = nullptr;

        Details details;
        Flags flags;

        UniformBuffer* transform = nullptr;

        // Create
        Canvas(NativeWindow* window = nullptr) {

            this->window = window;

            //glfwMakeContextCurrent(window);
            //glfwSwapInterval(1);

            window->makeContextCurrent();

            // 2. Initialize GLEW
            glewExperimental = GL_TRUE; // Enable core profiles
            GLenum glewStatus = glewInit();

            if (glewStatus != GLEW_OK) {
                const GLubyte* errorStr = glewGetErrorString(glewStatus);
                throw std::runtime_error(std::string("[Canvas] Glew init failed: ") + reinterpret_cast<const char*>(errorStr));
            }

            dbg("GLEW version: %s\n", glewGetString(GLEW_VERSION));
            dbg("OpenGL version: %s\n", glGetString(GL_VERSION));
            dbg("GLSL version: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
            dbg("Renderer: %s\n", glGetString(GL_RENDERER));
            dbg("Vendor: %s\n", glGetString(GL_VENDOR));

            glEnable(GL_MULTISAMPLE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            transform = new UniformBuffer(sizeof(glm::mat4));
        }

        // Destroy
        ~Canvas() {
            
        }

        void draw() {
            
            if (!window) return;

            if (flags.resize) {

                glViewport(0, 0, details.width, details.height);

                glm::mat4 projection = glm::ortho(
                    0.0f,           // left
                    static_cast<float>(details.width),   // right
                    static_cast<float>(details.height),  // bottom
                    0.0f,           // top (flipped for top-left origin)
                    -1.0f,          // near
                    1.0f            // far
                );

                transform->set(&projection);

                flags.resize = false;
            }

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);  // Transparent black
            glClear(GL_COLOR_BUFFER_BIT);

            transform->bind(0);
        }

        void flush() {
            //glfwSwapBuffers(window);
            window->swapBuffers();
        }
    };
};