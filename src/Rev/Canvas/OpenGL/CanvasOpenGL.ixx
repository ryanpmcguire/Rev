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

        GLFWwindow* window = nullptr;

        Flags flags;

        UniformBuffer* transform = nullptr;
        
        Rectangle* rectangle = nullptr;
        Rectangle* rect2 = nullptr;

        std::vector<Primitive*> primitives;

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

                int width, height;
                glfwGetFramebufferSize(window, &width, &height);
                glViewport(0, 0, width, height);

                glm::mat4 projection = glm::ortho(
                    0.0f,           // left
                    static_cast<float>(width),   // right
                    static_cast<float>(height),  // bottom
                    0.0f,           // top (flipped for top-left origin)
                    -1.0f,          // near
                    1.0f            // far
                );

                transform->set(&projection);

                flags.resize = false;
            }

            // Clear screen (customize as needed)
            glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            transform->bind(0);

            // Draw your OpenGL content here
            //rectangle->draw();
            //rect2->draw();

            for (Primitive* primitive : primitives) {
                primitive->draw();
            }
        }

        void flush() {
            glfwSwapBuffers(window);
        }
    };
};