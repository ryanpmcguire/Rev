module;

#include <cstddef>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <dbg.hpp>

#include "Helpers/MetalBackend.hpp"

export module Rev.Metal.Canvas;

import Rev.NativeWindow;
import Rev.Metal.Pipeline;
import Rev.Metal.UniformBuffer;

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
        MetalContext* context = nullptr;

        Details details;
        Flags flags;

        UniformBuffer* transform = nullptr;

        Canvas(NativeWindow* w = nullptr) {

            window = w;

            if (window) {
                context = metal_context_create(window->handle);
            }

            transform = new UniformBuffer(context, sizeof(glm::mat4));
        }

        ~Canvas() {
            if (context) { metal_context_destroy(context); }
        }

        void beginFrame() {

            dbg("[Canvas] drawing!");

            if (!window || !context) { return; }

            if (flags.resize) {

                details.width = window->size.w;
                details.height = window->size.h;

                metal_context_resize(context, details.width, details.height);

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

            // Instead of glClear, call Metal clear
            metal_begin_frame(context); // red

            transform->bind(context, 0);
        }

        void endFrame() {
            if (context) {
                metal_end_frame(context);
            }
        }

        void drawArrays(Pipeline::Topology topology, size_t start, size_t verticesPer) {
            //glDrawArrays(topology, start, verticesPer);
        }

        void drawArraysInstanced(Pipeline::Topology topology, size_t start, size_t verticesPer, size_t numInstances) {
            metal_draw_arrays_instanced(context, topology, start, verticesPer, numInstances);
        }
    };
};