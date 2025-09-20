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

            // Determine format
            /*GLenum format = GL_RED;
            if (channels == 3) { format = GL_RGB; }
            else if (channels == 4) { format = GL_RGBA; }

            // Generate and bind texture
            glGenTextures(1, &id);
            glBindTexture(GL_TEXTURE_2D, id);

            glTexImage2D(
                GL_TEXTURE_2D, 0, format,
                static_cast<GLsizei>(width),
                static_cast<GLsizei>(height),
                0, format, GL_UNSIGNED_BYTE, data
            );

            // Set swizzle to ensure correct mapping for single-channel textures
            if (channels == 1) {
                GLint swizzleMask[] = { GL_RED, GL_RED, GL_RED, GL_RED };
                glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);
            }

            // Set default filtering
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

            // Set clamping to edge
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            glBindTexture(GL_TEXTURE_2D, 0);*/
        }

        // Destroy
        ~Texture() {

            /*if (id) { glDeleteTextures(1, &id); }*/
        }

        void bind(size_t pos) {
            /*glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, id);*/
        }

        void unbind(size_t pos) {
            /*glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, 0);*/
        }
    };
};