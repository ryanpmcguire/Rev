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
        
        //GLuint vaoID = 0;
        //GLuint bufferID = 0;

        void* context = nullptr;
        void* buffer = nullptr;

        void* data = nullptr;
        size_t vertSize = sizeof(Vertex);
        size_t num = 0;
        size_t size = 0;
        size_t divisor = 0;

        VertexBuffer(void* context, size_t num, size_t vertSize = sizeof(Vertex), size_t divisor = 0) {

            this->context = context;

            this->num = num;
            this->vertSize = vertSize;
            this->size = num * vertSize;
            this->divisor = divisor;

            // Create buffer, map data
            buffer = metal_create_vertex_buffer((MetalContext*)context, size);
            data = metal_map_vertex_buffer(buffer);

            //glGenVertexArrays(1, &vaoID);
            //glBindVertexArray(vaoID);

            this->resize(num);
        }

        ~VertexBuffer() {

            if (buffer) {
                metal_destroy_vertex_buffer(buffer);
            }

            /*if (data) {
                glBindBuffer(GL_ARRAY_BUFFER, bufferID);
                glUnmapBuffer(GL_ARRAY_BUFFER); // optional if persistent
            }

            if (bufferID) {
                glDeleteBuffers(1, &bufferID);
            }*/
        }

        Vertex* verts() {
            return static_cast<Vertex*>(data);
        }

        void set(std::vector<Vertex> newVertices) {
            memcpy(data, newVertices.data(), size);
        }

        void resize(size_t newNum) {

            this->num = newNum;
            this->size = newNum * vertSize;
        }

        void bind(void* context) {
            metal_bind_vertex_buffer((MetalContext*)context, buffer, 0);
        }

        void unbind() {
            
        }
    };
};