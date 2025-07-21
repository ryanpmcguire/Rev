module;

#include <glew/glew.h>
#include <cstring>

export module Rev.OpenGL.UniformBuffer;

export namespace Rev {

    struct UniformBuffer {

        GLuint bufferID = 0;
        void* data = nullptr;
        size_t size = 0;

        UniformBuffer(size_t size) {

            this->size = size;

            glGenBuffers(1, &bufferID);
            glBindBuffer(GL_UNIFORM_BUFFER, bufferID);

            glBufferStorage(GL_UNIFORM_BUFFER, size, nullptr,
                GL_MAP_WRITE_BIT |
                GL_MAP_PERSISTENT_BIT |
                GL_MAP_COHERENT_BIT
            );

            data = glMapBufferRange(GL_UNIFORM_BUFFER, 0, size,
                GL_MAP_WRITE_BIT |
                GL_MAP_PERSISTENT_BIT |
                GL_MAP_COHERENT_BIT
            );

            glBindBuffer(GL_UNIFORM_BUFFER, 0);
        }

        ~UniformBuffer() {
            if (data) {
                glBindBuffer(GL_UNIFORM_BUFFER, bufferID);
                glUnmapBuffer(GL_UNIFORM_BUFFER);
            }

            if (bufferID) {
                glDeleteBuffers(1, &bufferID);
            }
        }

        void set(void* value) {
            memcpy(data, value, size);
        }

        void bind(GLuint bindingPoint) {
            glBindBufferBase(GL_UNIFORM_BUFFER, bindingPoint, bufferID);
        }

        void unbind() {
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
        }
    };
};
