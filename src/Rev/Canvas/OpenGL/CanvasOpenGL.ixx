module;

#include <vector>
#include <glew/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <dbg.hpp>

export module Rev.OpenGL.Canvas;

import Rev.OpenGL.Primitive;
import Rev.OpenGL.UniformBuffer;
import Rev.OpenGL.Rectangle;

export namespace Rev {

    struct Canvas {

        struct Flags {
            bool resize = true;
            bool record = true;
        };

        struct Details {
            int width, height;
        };

        GLFWwindow* window = nullptr;

        Details details;
        Flags flags;

        UniformBuffer* transform = nullptr;
        
        Rectangle* rectangle = nullptr;
        Rectangle* rect2 = nullptr;

        std::vector<Primitive*> primitives;

        // Create
        Canvas(GLFWwindow* window = nullptr) {

            this->window = window;

            glfwMakeContextCurrent(window);
            glfwSwapInterval(1);

            // 2. Initialize GLEW
            glewExperimental = GL_TRUE; // Enable core profiles
            GLenum glewStatus = glewInit();

            if (glewStatus != GLEW_OK) {
                const GLubyte* errorStr = glewGetErrorString(glewStatus);
                dbg("GLEW init failed: %s\n", errorStr);
                //std::exit(1); // Optional, but recommended to bail
            }

            glEnable(GL_MULTISAMPLE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            transform = new UniformBuffer(sizeof(glm::mat4));
        }

        // Destroy
        ~Canvas() {
            
            delete rectangle;
        }

        void draw(Primitive* primitive) {
            primitives.push_back(primitive);
        }

        void draw() {
            
            if (!window) return;

            if (flags.resize) {

                glfwGetFramebufferSize(window, &details.width, &details.height);
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
            glfwSwapBuffers(window);
        }
    };
};