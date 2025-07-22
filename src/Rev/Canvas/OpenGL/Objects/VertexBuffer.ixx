module;

#include <vector>
#include <glew/glew.h>
#include <GLFW/glfw3.h>

export module Rev.OpenGL.VertexBuffer;

export namespace Rev {

    struct VertexBuffer {

        struct Vertex {
            float x, y;
        };
        
        GLuint vaoID = 0;
        GLuint bufferID = 0;

        void* data = nullptr;
        size_t size = 0;

        VertexBuffer(size_t num) {

            this->size = num * sizeof(Vertex);

            glGenVertexArrays(1, &vaoID);
            glBindVertexArray(vaoID);

            glGenBuffers(1, &bufferID);
            glBindBuffer(GL_ARRAY_BUFFER, bufferID);

            // Allocate persistent buffer
            glBufferStorage(GL_ARRAY_BUFFER, size, nullptr,
                GL_MAP_WRITE_BIT |
                GL_MAP_PERSISTENT_BIT |
                GL_MAP_COHERENT_BIT
            );

            // Map it
            data = glMapBufferRange(GL_ARRAY_BUFFER, 0, size,
                GL_MAP_WRITE_BIT |
                GL_MAP_PERSISTENT_BIT |
                GL_MAP_COHERENT_BIT
            );

            // Configure vertex attribute inside VAO
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);

            glBindVertexArray(0);
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

        void bind() {
            glBindVertexArray(vaoID);
        }

        void unbind() {
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
    };
};