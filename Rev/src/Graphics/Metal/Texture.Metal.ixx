module;

#include <cstddef>

export module Rev.Metal.Texture;

export namespace Rev {

    struct Texture {

        unsigned char* data = nullptr;
        size_t width, height;
        size_t channels;
        size_t size = 0;

        //GLuint id = 0;

        // Create
        Texture(unsigned char* data, size_t width, size_t height, size_t channels)
        : data(data), width(width), height(height), channels(channels) {

        }

        // Destroy
        ~Texture() {

           
        }

        void bind(void* context, size_t pos) {
            
        }

        void unbind(size_t pos) {
            
        }
    };
};