module;

#include <vector>
#include <glew/glew.h>

export module Rev.OpenGL.VertexBuffer;

export namespace Rev {

    struct VertexBuffer {

        struct Vertex {
            float x, y;
        };
        
        GLuint vaoID = 0;
        GLuint bufferID = 0;

        void* data = nullptr;
        size_t vertSize = sizeof(Vertex);
        size_t num = 0;
        size_t size = 0;
        size_t divisor = 0;

        VertexBuffer(void* context, size_t num, size_t vertSize = sizeof(Vertex), size_t divisor = 0) {

            this->num = num;
            this->vertSize = vertSize;
            this->size = num * vertSize;
            this->divisor = divisor;

            glGenVertexArrays(1, &vaoID);
            glBindVertexArray(vaoID);

            this->resize(num);
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

        Vertex* verts() {
            return static_cast<Vertex*>(data);
        }

        void set(std::vector<Vertex> newVertices) {
            memcpy(data, newVertices.data(), size);
        }

        void resize(size_t newNum) {

            this->num = newNum;
            this->size = newNum * vertSize;
        
            // Delete previous buffer
            if (data) {
                glBindBuffer(GL_ARRAY_BUFFER, bufferID);
                glUnmapBuffer(GL_ARRAY_BUFFER);
                data = nullptr;
            }

            if (bufferID) {
                glDeleteBuffers(1, &bufferID);
                bufferID = 0;
            }
        
            glBindVertexArray(vaoID);
        
            glGenBuffers(1, &bufferID);
            glBindBuffer(GL_ARRAY_BUFFER, bufferID);
        
            glBufferStorage(GL_ARRAY_BUFFER, size, nullptr,
                GL_MAP_WRITE_BIT |
                GL_MAP_PERSISTENT_BIT |
                GL_MAP_COHERENT_BIT
            );
        
            data = glMapBufferRange(GL_ARRAY_BUFFER, 0, size,
                GL_MAP_WRITE_BIT |
                GL_MAP_PERSISTENT_BIT |
                GL_MAP_COHERENT_BIT
            );
        
            // Re-specify the attribute pointer
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, vertSize / sizeof(float), GL_FLOAT, GL_FALSE, vertSize, (void*)0);
        
            if (divisor) {
                glVertexAttribDivisor(0, divisor);
            }
        
            glBindVertexArray(0);
        }

        void bind() {
            glBindVertexArray(vaoID);
        }

        void unbind() {
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
    };
};