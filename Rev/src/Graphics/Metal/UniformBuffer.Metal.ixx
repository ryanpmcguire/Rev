module;

#include <cstddef>
#include <cstring>

#include "./Helpers/MetalBackend.hpp"

export module Rev.Metal.UniformBuffer;

export namespace Rev {

    struct UniformBuffer {

        //GLuint bufferID = 0;
        void* context = nullptr;
        void* buffer = nullptr;
        void* data = nullptr;
        size_t size = 0;

        UniformBuffer(void* context, size_t size) {

            this->size = size;
            this->context = context;

            // Create buffer, map data
            buffer = metal_create_uniform_buffer((MetalContext*)context, size);
            data = metal_map_uniform_buffer(buffer);
        }

        ~UniformBuffer() {

            metal_destroy_uniform_buffer(buffer);
            data = nullptr;
        }

        void set(void* value) {
            std::memcpy(data, value, size);
        }

        void bind(void* context, size_t pos) {
            metal_bind_uniform_buffer((MetalContext*)context, buffer, pos);
        }

        void unbind() {
            //glBindBuffer(GL_UNIFORM_BUFFER, 0);
        }
    };
};