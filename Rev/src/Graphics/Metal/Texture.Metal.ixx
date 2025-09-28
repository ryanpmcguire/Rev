module;

#include <cstddef>

#include "./Helpers/MetalBackend.hpp"

export module Rev.Metal.Texture;

export namespace Rev {

    struct Texture {

        unsigned char* data = nullptr;
        size_t width = 0, height = 0;
        size_t channels = 0;
        size_t size = 0;

        void* handle = nullptr;

        // Create
        Texture(void* context, unsigned char* data, size_t width, size_t height, size_t channels)
        : data(data), width(width), height(height), channels(channels) {

            // By default, use nearest filtering (matches OpenGL implementation)
            handle = metal_create_texture(
                (MetalContext*)context,
                static_cast<const unsigned char*>(data),
                static_cast<int>(width),
                static_cast<int>(height),
                static_cast<int>(channels)
            );
        }

        // Destroy
        ~Texture() {
            if (handle) {
                metal_destroy_texture(handle);
                handle = nullptr;
            }
        }

        void bind(void* context, int unit = 0) {
            if (handle) {
                metal_bind_texture((MetalContext*)context, handle, unit);
            }
        }

        void unbind(int unit = 0) {
            // Metal doesn't have a global unbind like OpenGL.
            // We could set a nil texture/sampler at this slot if desired.
            // For now, no-op to keep API symmetry.
        }
    };
};
