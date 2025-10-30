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
            bool stencil = false;
            bool color = false;
        };

        struct Details {
            size_t width, height;
            float scale = 1.0f;
        };

        // Context management
        void* context = nullptr;  // (context is unused)
        NativeWindow* window = nullptr;
        UniformBuffer* transform = nullptr;
        FrameBuffer* frameBuffer = nullptr;

        // Configurable details
        Details details;
        Flags flags;

        // Create
        Canvas(NativeWindow* window = nullptr) {

            this->window = window;

            window->makeContextCurrent();
            window->loadGlFunctions();

            glEnable(GL_MULTISAMPLE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            transform = new UniformBuffer(context, sizeof(glm::mat4));
            frameBuffer = new FrameBuffer(context, { .width = 1, .height = 1 });
        }

        // Destroy
        ~Canvas() {
            delete transform;
            delete frameBuffer;
        }

        // Frame setup / blitting
        //--------------------------------------------------

        void beginFrame() {
            
            if (!window) { return; }

            // Ensure cache coherency (wait for flush) before proceeding
            // (this is because any changes to buffers need to make it to
            // ram before we can tell the GPU everything is good)
            glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT | GL_UNIFORM_BARRIER_BIT);

            // If canvas needs to adjust size to window
            if (flags.resize) {

                // Get width and height from window size
                details.width = window->size.w;
                details.height = window->size.h;
                details.scale = window->scale;

                glViewport(0, 0, (GLint)std::round(details.width), (GLint)std::round(details.height));

                glm::mat4 projection = glm::ortho(
                    0.0f, static_cast<float>(details.width) / details.scale - 0.5f,     // Left / right
                    static_cast<float>(details.height) / details.scale - 0.5f, 0.0f,    // Bottom / top
                    -1.0f, 1.0f                                                         // Near / far
                );

                // Resize framebuffer, bind transform
                frameBuffer->resize(details.width, details.height);
                transform->set(&projection);
                flags.resize = false;
            }

            // Framebuffer
            frameBuffer->bind();
            glEnable(GL_MULTISAMPLE);
            glDisable(GL_DEPTH_TEST);

            // Blend func and color
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBlendColor(1.0f, 1.0f, 1.0f, 1.0f);

            // Stencil
            glEnable(GL_STENCIL_TEST);
            glStencilMask(0xFF);

            // Clear before drawing
            glClearStencil(0x00);
            glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

            transform->bind(0);
        }

        // We end the frame by blitting and swapping buffers (present)
        void endFrame() {

            // Bind both render target and actual (window) framebuffer
            glBindFramebuffer(GL_READ_FRAMEBUFFER, frameBuffer->buffer);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

            // Copy (blit)
            glBlitFramebuffer(
                0, 0, details.width, details.height,
                0, 0, details.width, details.height,
                GL_COLOR_BUFFER_BIT, GL_NEAREST
            );

            // Unbind and swap
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            window->swapBuffers();
        }

        // Stencil management
        //--------------------------------------------------

        // Enable / disable writing to color buffer
        void colorWrite(bool enable) {

            // Avoid redundant state changes
            if (enable == flags.color) { return; }
            else { flags.color = enable; }

            // Set color mask to enable/disable writing
            if (enable) { glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); }
            else { glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); }
        }

        // Enable / disable writing to stencil buffer
        void stencilWrite(bool enable) {

            // Avoid redundant state changes
            if (enable == flags.stencil ) { return; }
            else { flags.stencil = enable; }

            // Set stencil mask to enable/disable writing
            if (enable) { glStencilMask(0xFF); }
            else { glStencilMask(0x00); }
        }

        // Set stencil depth
        void stencilDepth(size_t value) {
            glStencilFunc(GL_LEQUAL, value, 0xFF);
        }

        // Set to all zeroes
        void stencilClear() {
            glClearStencil(0.0f);
            glClear(GL_STENCIL_BUFFER_BIT);
        }

        // Fill stencil buffer with uniform value(s)
        void stencilFill(size_t value) {
            glClearStencil(value);
            glClear(GL_STENCIL_BUFFER_BIT);
        }

        // Pushing to stencil (increasing depth where test passes)
        void stencilPush(size_t depth) {
            glStencilFunc(GL_LEQUAL, depth, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
        }

        // Popping from stencil (decreasing depth where test passes)
        void stencilPop(size_t depth) {
            glStencilFunc(GL_LEQUAL, depth, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_DECR);
        }

        // Setting stencil (set depth where test passes)
        void stencilSet(size_t depth) {
            glStencilFunc(GL_LEQUAL, depth, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        }

        // Drawing functions
        //--------------------------------------------------

        void drawArrays(Pipeline::Topology topology, size_t start, size_t verticesPer) {
            glDrawArrays(topology, start, verticesPer);
        }

        void drawArraysInstanced(Pipeline::Topology topology, size_t start, size_t verticesPer, size_t numInstances) {
            glDrawArraysInstanced(GL_TRIANGLE_FAN, start, verticesPer, numInstances);
        }
    };
};