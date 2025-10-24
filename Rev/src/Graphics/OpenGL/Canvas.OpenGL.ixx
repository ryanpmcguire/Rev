module;

#include <stdexcept>
#include <vector>
#include <glew/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <dbg.hpp>

export module Rev.Graphics.Canvas;

import Rev.NativeWindow;
import Rev.Graphics.FrameBuffer;
import Rev.Graphics.Pipeline;
import Rev.Graphics.UniformBuffer;

export namespace Rev::Graphics {

    struct Canvas {

        struct Flags {
            bool resize = true;
            bool record = true;
            bool stencil = true;
        };

        struct Details {
            size_t width, height;
            float scale = 1.0f;
        };

        NativeWindow* window = nullptr;

        Details details;
        Flags flags;

        void* context = nullptr;                        // Context is unused
        UniformBuffer* transform = nullptr;

        // Render target
        //--------------------------------------------------

        FrameBuffer* frameBuffer = nullptr;

        // Create
        Canvas(NativeWindow* window = nullptr) {

            this->window = window;

            window->makeContextCurrent();
            window->loadGlFunctions();

            glEnable(GL_MULTISAMPLE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            transform = new UniformBuffer(context, sizeof(glm::mat4));

            frameBuffer = new FrameBuffer(context, {
                .width = 1, .height = 1
            });
        }

        // Destroy
        ~Canvas() {
            delete transform;
        }

        void beginFrame() {
            
            if (!window) { return; }

            glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT | GL_UNIFORM_BARRIER_BIT);

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

                frameBuffer->resize(details.width, details.height);
                transform->set(&projection);

                //dbg("[Canvas] Resize: %i, %i", details.width, details.height);

                flags.resize = false;
            }

            frameBuffer->bind();

            // Framebuffer
            glEnable(GL_MULTISAMPLE);

            // Blend func and color
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBlendColor(1.0f, 1.0f, 1.0f, 1.0f);

            // Stencil
            glEnable(GL_STENCIL_TEST);
            glStencilFunc(GL_NOTEQUAL, 0, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

            // Clear before drawing
            glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

            stencilWrite(true);
            fillStencil(1);
            stencilWrite(false);

            transform->bind(0);
        }

        void endFrame() {

            // Blit from our framebuffer to the default window framebuffer
            glBindFramebuffer(GL_READ_FRAMEBUFFER, frameBuffer->fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0); // Default framebuffer

            glBlitFramebuffer(
                0, 0, details.width, details.height,
                0, 0, details.width, details.height,
                GL_COLOR_BUFFER_BIT, GL_NEAREST
            );

            // Unbind
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            window->swapBuffers();
        }

        void clearStencil() {
            glClear(GL_STENCIL_BUFFER_BIT);
        }

        void fillStencil(uint8_t value) {
            glClearStencil(value);                      // specify clear value
            glClear(GL_STENCIL_BUFFER_BIT);             // fill stencil with 'value'
        }

        void stencilWrite(bool enable) {
            
            // Return if state would not change
            if (enable == flags.stencil) { return; }
            else { flags.stencil = enable; }

            // Enable writing
            if (enable) {

                glStencilFunc(GL_ALWAYS, 1, 0xFF);   // Always pass
                glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);  // Replace stencil with refValue
                glStencilMask(0xFF);                        // Enable writing
            }

            // Disable writing
            else {
                glStencilFunc(GL_NOTEQUAL, 0, 0xFF);        // Or GL_EQUAL, depending on your logic
                glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);     // Don’t modify
                glStencilMask(0x00);                        // Disable writing
            }
        }

        void drawArrays(Pipeline::Topology topology, size_t start, size_t verticesPer) {
            glDrawArrays(topology, start, verticesPer);
        }

        void drawArraysInstanced(Pipeline::Topology topology, size_t start, size_t verticesPer, size_t numInstances) {
            glDrawArraysInstanced(GL_TRIANGLE_FAN, start, verticesPer, numInstances);
        }
    };
};