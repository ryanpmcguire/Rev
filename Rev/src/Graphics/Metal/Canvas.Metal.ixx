module;

#include <cstddef>
#include <dbg.hpp>
#include "Helpers/MetalBackend.hpp"

export module Rev.Metal.Canvas;

import Rev.NativeWindow;
import Rev.Graphics.Pipeline;

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
        MetalBackend* backend = nullptr;
        Details details;
        Flags flags;

        Canvas(NativeWindow* w = nullptr) {
            window = w;
            if (window) {
                backend = metal_backend_create(window->handle);
            }
        }

        ~Canvas() {
            if (backend) metal_backend_destroy(backend);
        }

        void draw() {

            dbg("[Canvas] drawing!");

            if (!window || !backend) return;

            if (flags.resize) {
                details.width = window->size.w;
                details.height = window->size.h;
                metal_backend_resize(backend, details.width, details.height);
                flags.resize = false;
            }

            // Instead of glClear, call Metal clear
            metal_backend_clear(backend, 1.0f, 0.0f, 0.0f, 1.0f); // red
        }

        void flush() {
            if (backend) {
                metal_backend_present(backend);
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