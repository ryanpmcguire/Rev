module;

#include <cstddef>
#include <vector>

#include "./Helpers/MetalBackend.hpp"

export module Rev.Metal.VertexBuffer;

export namespace Rev {

    struct VertexBuffer {

        struct Vertex {
            float x, y;
        };

        void* context = nullptr;
        void* buffer = nullptr;

        void* data = nullptr;
        size_t vertSize = sizeof(Vertex);
        size_t num = 0;
        size_t size = 0;
        size_t divisor = 0;

        VertexBuffer(void* context, size_t vertSize = sizeof(Vertex), size_t divisor = 0, size_t num = 0) {

            this->context = context;

            this->vertSize = vertSize;
            this->divisor = divisor;

            this->resize(num);
        }

        ~VertexBuffer() {

            if (buffer) {
                metal_destroy_vertex_buffer(buffer);
            }
        }

        Vertex* verts() {
            return static_cast<Vertex*>(data);
        }

        void set(std::vector<Vertex> newVertices) {
            memcpy(data, newVertices.data(), size);
        }

        void resize(size_t newNum) {

            if (newNum == num) { return; }

            this->num = newNum;
            this->size = num * vertSize;

            if (buffer) { metal_destroy_vertex_buffer(buffer); }

            // Create buffer, map data
            buffer = metal_create_vertex_buffer((MetalContext*)context, size);
            data = metal_map_vertex_buffer(buffer);
        }

        void bind() {
            metal_bind_vertex_buffer((MetalContext*)context, buffer, 0);
        }

        void unbind() {
            
        }
    };
};