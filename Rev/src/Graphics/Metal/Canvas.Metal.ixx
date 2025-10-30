module;

#include <cstddef>
#include <iostream>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <dbg.hpp>

#include "Helpers/MetalBackend.hpp"

export module Rev.Graphics.Canvas;

import Rev.NativeWindow;
import Rev.Graphics.Pipeline;
import Rev.Graphics.UniformBuffer;
import Rev.Graphics.FrameBuffer;

export namespace Rev::Graphics {

    struct Canvas {

        struct Flags {
            bool resize = true;
            bool record = true;
            bool stencil = false;
            bool color = false;
        };

        struct Details {
            int width, height;
            float scale = 1.0f;
        };

        // Context management
        NativeWindow* window = nullptr;
        MetalContext* context = nullptr;
        UniformBuffer* transform = nullptr;
        FrameBuffer* frameBuffer = nullptr;

        // Configurable details
        Details details;
        Flags flags;

        Canvas(NativeWindow* w = nullptr) {

            window = w;

            if (window) {

                context = metal_context_create(window->handle);

                details.width = window->size.w;
                details.height = window->size.h;
                details.scale = metal_context_get_scale(context);
            }

            transform = new UniformBuffer(context, sizeof(glm::mat4));
            frameBuffer = new FrameBuffer(context, { .width = 1, .height = 1 });
        }

        ~Canvas() {

            delete transform;
            delete frameBuffer;

            if (context) { metal_context_destroy(context); }
        }

        // Frame setup / blitting
        //--------------------------------------------------

        void beginFrame() {

            //dbg("[Canvas] drawing!");

            if (!window || !context) {
                return;
            }

            if (flags.resize) {

                details.width = window->size.w;
                details.height = window->size.h;
                details.scale = metal_context_get_scale(context);

                metal_context_resize(context, details.width, details.height);

                glm::mat4 projection = glm::ortho(
                    0.0f,           // left
                    static_cast<float>(details.width),   // right
                    static_cast<float>(details.height),  // bottom
                    0.0f,           // top (flipped for top-left origin)
                    -1.0f,          // near
                    1.0f            // far
                );

                frameBuffer->resize(details.width, details.height);
                transform->set(glm::value_ptr(projection));
                flags.resize = false;
            }

            metal_framebuffer_begin_frame(context, frameBuffer->buffer);

            transform->bind(0);
        }

        void endFrame() {

            metal_framebuffer_end_frame(context, frameBuffer->buffer);
            metal_present(context, frameBuffer->buffer);

            //metal_begin_frame(context);
            //metal_framebuffer_blit_to_drawable(context, frameBuffer->buffer);
            //metal_end_frame(context);
        }

        // Stencil management
        //--------------------------------------------------

        // Enable / disable writing to color buffer
        void colorWrite(bool enable) {

            // Avoid redundant state changes
            if (enable == flags.color) { return; }
            else { flags.color = enable; }

        }

        // Enable / disable writing to stencil buffer
        void stencilWrite(bool enable) {

            // Avoid redundant state changes
            if (enable == flags.stencil ) { return; }
            else { flags.stencil = enable; }

        }

        // Set stencil depth
        void stencilDepth(size_t value) {

        }

        // Set to all zeroes
        void stencilClear() {

        }

        // Fill stencil buffer with uniform value(s)
        void stencilFill(size_t value) {

        }

        // Pushing to stencil (increasing depth where test passes)
        void stencilPush(size_t depth) {

        }

        // Popping from stencil (decreasing depth where test passes)
        void stencilPop(size_t depth) {

        }

        // Setting stencil (set depth where test passes)
        void stencilSet(size_t depth) {

        }

        // Drawing functions
        //--------------------------------------------------

        void drawArrays(Pipeline::Topology topology, size_t start, size_t verticesPer) {
            metal_draw_arrays(context, topology, start, verticesPer);
        }

        void drawArraysInstanced(Pipeline::Topology topology, size_t start, size_t verticesPer, size_t numInstances) {
            metal_draw_arrays_instanced(context, topology, start, verticesPer, numInstances);
        }
    };
};