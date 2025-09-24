module;

#include <cstddef>
#include <dbg.hpp>
#include "Helpers/MetalBackend.hpp"

export module Rev.Metal.Canvas;

import Rev.NativeWindow;
import Rev.Metal.Pipeline;

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

        Canvas(NativeWindow* w = nullptr) {

            window = w;

            if (window) {
                context = metal_context_create(window->handle);
            }
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
                flags.resize = false;
            }

            // Instead of glClear, call Metal clear
            metal_begin_frame(context); // red
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
            //glDrawArraysInstanced(GL_TRIANGLE_FAN, start, verticesPer, numInstances);
        }
    };
};