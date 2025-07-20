module;

#include <glew/glew.h>
#include <GLFW/glfw3.h>

export module Rev.OpenGL.VertexBuffer;

export namespace Rev {

    struct VertexBuffer {
        
        GLuint bufferID = 0;

        void* data = nullptr;
        size_t size = 0;

        VertexBuffer(size_t size) {

            this->size = size;

            glGenBuffers(1, &bufferID);
            glBindBuffer(GL_ARRAY_BUFFER, bufferID);

            // Allocate persistent, coherent buffer
            glBufferStorage(GL_ARRAY_BUFFER, size,
                nullptr, // no initial data
                GL_MAP_WRITE_BIT |
                GL_MAP_PERSISTENT_BIT |
                GL_MAP_COHERENT_BIT
            );

            // Persistently map the whole buffer
            data = glMapBufferRange(GL_ARRAY_BUFFER, 0, size,
                GL_MAP_WRITE_BIT |
                GL_MAP_PERSISTENT_BIT |
                GL_MAP_COHERENT_BIT
            );
        }

        ~VertexBuffer() {

            if (data) {
                glBindBuffer(GL_ARRAY_BUFFER, bufferID);
                glUnmapBuffer(GL_ARRAY_BUFFER); // optional if persistent
            }

            if (bufferID) {
                glDeleteBuffers(1, &bufferID);
            }
        }

        void bind() {
            glBindBuffer(GL_ARRAY_BUFFER, bufferID);
        }

        void unbind() {
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
    };
};