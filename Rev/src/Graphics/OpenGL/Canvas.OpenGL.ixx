module;

#include <stdexcept>
#include <vector>
#include <glew/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <dbg.hpp>

export module Rev.OpenGL.Canvas;

import Rev.NativeWindow;
import Rev.Graphics.RenderCommandEncoder;
import Rev.Graphics.Pipeline;
import Rev.Graphics.UniformBuffer;

export namespace Rev {

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
        RenderCommandEncoder* encoder = nullptr;
        UniformBuffer* transform = nullptr;

        // Create
        Canvas(NativeWindow* window = nullptr) {

            this->window = window;

            window->makeContextCurrent();
            window->loadGlFunctions();

            glEnable(GL_MULTISAMPLE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            encoder = new RenderCommandEncoder();
            transform = new UniformBuffer(context, sizeof(glm::mat4));
        }

        // Destroy
        ~Canvas() {
            
        }

        void beginFrame() {
            
            if (!window) { return; }

            // If canvas needs to adjust size to window
            if (flags.resize) {

                // Get width and height from window size
                details.width = window->size.w;
                details.height = window->size.h;
                details.scale = window->scale;

                glViewport(0, 0, details.width, details.height);

                glm::mat4 projection = glm::ortho(
                    0.0f,           // left
                    static_cast<float>(details.width) / details.scale,   // right
                    static_cast<float>(details.height) / details.scale,  // bottom
                    0.0f,           // top (flipped for top-left origin)
                    -1.0f,          // near
                    1.0f            // far
                );

                transform->set(&projection);

                flags.resize = false;
            }

            glEnable(GL_MULTISAMPLE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);  // Transparent black
            glClear(GL_COLOR_BUFFER_BIT);

            GLint samples = 0;
            glGetIntegerv(GL_SAMPLES, &samples);
            dbg("MSAA samples: %d\n", samples);

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