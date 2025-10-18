module;

#include <stdexcept>
#include <vector>
#include <glew/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <dbg.hpp>

export module Rev.Graphics.Canvas;

import Rev.NativeWindow;
import Rev.Graphics.Pipeline;
import Rev.Graphics.UniformBuffer;

export namespace Rev::Graphics {

    struct Canvas {

        struct Flags {
            bool resize = true;
            bool record = true;
        };

        struct Details {
            int width, height;
            float scale = 1.0f;
        };

        NativeWindow* window = nullptr;

        Details details;
        Flags flags;

        void* context = nullptr;                        // Context is unused
        UniformBuffer* transform = nullptr;

        // Create
        Canvas(NativeWindow* window = nullptr) {

            this->window = window;

            window->makeContextCurrent();
            window->loadGlFunctions();

            glEnable(GL_MULTISAMPLE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            transform = new UniformBuffer(context, sizeof(glm::mat4));
        }

        // Destroy
        ~Canvas() {
            delete transform;
        }

        void beginFrame() {
            
            if (!window) { return; }

            // If canvas needs to adjust size to window
            if (flags.resize) {

                // Get width and height from window size
                details.width = window->size.w;
                details.height = window->size.h;
                details.scale = window->scale;

                glViewport(0, 0, (GLint)std::round(details.width), (GLint)std::round(details.height));

                glm::mat4 projection = glm::ortho(
                    0.0f,           // left
                    static_cast<float>(details.width) / details.scale - 0.5f,   // right
                    static_cast<float>(details.height) / details.scale - 0.5f,  // bottom
                    0.0f,           // top (flipped for top-left origin)
                    -1.0f,          // near
                    1.0f            // far
                );

                transform->set(&projection);

                //dbg("[Canvas] Resize: %i, %i", details.width, details.height);

                flags.resize = false;
            }

            glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT | GL_UNIFORM_BARRIER_BIT);

            glEnable(GL_MULTISAMPLE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBlendColor(1.0f, 1.0f, 1.0f, 1.0f);
            glClearColor(1.0f, 1.0f, 1.0f, 1.0f);  // Transparent black
            glClear(GL_COLOR_BUFFER_BIT);

            transform->bind(0);
        }

        void endFrame() {
            window->swapBuffers();
        }

        void drawArrays(Pipeline::Topology topology, size_t start, size_t verticesPer) {
            glDrawArrays(topology, start, verticesPer);
        }

        void drawArraysInstanced(Pipeline::Topology topology, size_t start, size_t verticesPer, size_t numInstances) {
            glDrawArraysInstanced(GL_TRIANGLE_FAN, start, verticesPer, numInstances);
        }
    };
};