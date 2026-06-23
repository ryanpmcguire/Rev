module;

#include <cstring>
#include <stdexcept>
#include <glew/glew.h>

export module Rev.Graphics.TextureBuffer;

import Rev.NativeWindow;

export namespace Rev::Graphics {

    // A buffer texture (TBO): GPU buffer storage exposed to shaders as a
    // samplerBuffer via texelFetch. Persistent-mapped like VertexBuffer, so
    // callers write through `data` (or set()) when they prepare their geometry,
    // and the GPU reads it at draw time -- upload and draw stay separate.
    //
    // Format is GL_RG32F: one vec2 per element. Like Texture, the mutating paths
    // save and restore the previous GL bindings so global state is left intact.
    struct TextureBuffer {

        void* context = nullptr;

        GLuint id = 0;          // buffer texture
        GLuint bufferID = 0;    // backing buffer storage

        void* data = nullptr;
        size_t count = 0;       // number of vec2 elements
        size_t size = 0;        // bytes

        TextureBuffer(void* context) {

            this->context = context;
            NativeWindow::requireContext(context, "TextureBuffer construct");

            glGenTextures(1, &id);
        }

        ~TextureBuffer() {

            NativeWindow::requireContext(context, "TextureBuffer destroy");

            if (data) {
                GLint previousBuffer = 0;
                glGetIntegerv(GL_TEXTURE_BUFFER_BINDING, &previousBuffer);
                glBindBuffer(GL_TEXTURE_BUFFER, bufferID);
                glUnmapBuffer(GL_TEXTURE_BUFFER);
                glBindBuffer(GL_TEXTURE_BUFFER, static_cast<GLuint>(previousBuffer));
                data = nullptr;
            }

            if (bufferID) { glDeleteBuffers(1, &bufferID); }
            if (id)       { glDeleteTextures(1, &id); }
        }

        void resize(size_t newCount) {

            NativeWindow::requireContext(context, "TextureBuffer resize");

            if (newCount == count) { return; }
            count = newCount;
            size = count * 2 * sizeof(float);

            // Save state we are about to disturb.
            GLint previousActiveTexture = 0;
            GLint previousTextureBinding = 0;
            GLint previousBufferBinding = 0;
            glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
            glGetIntegerv(GL_TEXTURE_BINDING_BUFFER, &previousTextureBinding);
            glGetIntegerv(GL_TEXTURE_BUFFER_BINDING, &previousBufferBinding);

            // Immutable storage can't grow; tear down and rebuild.
            if (data) {
                glBindBuffer(GL_TEXTURE_BUFFER, bufferID);
                glUnmapBuffer(GL_TEXTURE_BUFFER);
                data = nullptr;
            }

            if (bufferID) { glDeleteBuffers(1, &bufferID); bufferID = 0; }

            if (size) {

                glGenBuffers(1, &bufferID);
                glBindBuffer(GL_TEXTURE_BUFFER, bufferID);

                glBufferStorage(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(size), nullptr,
                    GL_MAP_WRITE_BIT |
                    GL_MAP_PERSISTENT_BIT |
                    GL_MAP_COHERENT_BIT
                );

                data = glMapBufferRange(GL_TEXTURE_BUFFER, 0, static_cast<GLsizeiptr>(size),
                    GL_MAP_WRITE_BIT |
                    GL_MAP_PERSISTENT_BIT |
                    GL_MAP_COHERENT_BIT
                );

                if (!data) {
                    glDeleteBuffers(1, &bufferID);
                    bufferID = 0; size = 0; count = 0;
                    glBindBuffer(GL_TEXTURE_BUFFER, static_cast<GLuint>(previousBufferBinding));
                    throw std::runtime_error("[TextureBuffer] Failed to map buffer");
                }

                // Point the buffer texture at the (new) buffer object.
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_BUFFER, id);
                glTexBuffer(GL_TEXTURE_BUFFER, GL_RG32F, bufferID);
            }

            // Restore prior bindings.
            glBindTexture(GL_TEXTURE_BUFFER, static_cast<GLuint>(previousTextureBinding));
            glActiveTexture(static_cast<GLenum>(previousActiveTexture));
            glBindBuffer(GL_TEXTURE_BUFFER, static_cast<GLuint>(previousBufferBinding));
        }

        // Replace contents with `numElements` vec2s from interleaved xy floats.
        void set(const float* xy, size_t numElements) {

            resize(numElements);

            if (!data || !numElements) { return; }

            std::memcpy(data, xy, numElements * 2 * sizeof(float));
        }

        float* floats() {
            return static_cast<float*>(data);
        }

        void bind(GLuint unit = 0) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_BUFFER, id);
        }

        void unbind(GLuint unit = 0) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_BUFFER, 0);
        }
    };
};
