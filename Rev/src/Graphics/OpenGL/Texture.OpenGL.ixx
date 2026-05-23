module;

#include <glew/glew.h>
#include <stdexcept>
#include <string>

export module Rev.Graphics.Texture;

import Rev.NativeWindow;

export namespace Rev::Graphics {

    struct Texture {

        enum class Filter : GLint {
            
            Nearest                 = GL_NEAREST,
            Bilinear                = GL_LINEAR,
        
            NearestMipmapNearest    = GL_NEAREST_MIPMAP_NEAREST,
            BilinearMipmapNearest   = GL_LINEAR_MIPMAP_NEAREST,
            NearestMipmapBilinear   = GL_NEAREST_MIPMAP_LINEAR,
            Trilinear               = GL_LINEAR_MIPMAP_LINEAR
        };

        unsigned char* data = nullptr;
        size_t width, height;
        size_t channels;
        size_t size = 0;
        Filter filter;

        GLuint id = 0;
        void* context = nullptr;

        struct Params {
            
            unsigned char* data = nullptr;

            size_t width = 0, height = 0;
            size_t channels = 4;

            Filter filter = Filter::Nearest;
        };

        // Create
        Texture(void* context, Params params) {

            this->context = context;
            NativeWindow::requireContext(context, "Texture construct");

            data = params.data;
            width = params.width; height = params.height;
            channels = params.channels;
            filter = params.filter;

            // Determine format
            GLenum format = GL_RED;
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
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)filter);

            // Set clamping to edge
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            glBindTexture(GL_TEXTURE_2D, 0);
        }

        // Destroy
        ~Texture() {

            NativeWindow::requireContext(context, "Texture destroy");
            if (id) { glDeleteTextures(1, &id); }
        }

        void bind(GLuint unit = 0) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, id);
        }

        void unbind(GLuint unit = 0) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    };
};