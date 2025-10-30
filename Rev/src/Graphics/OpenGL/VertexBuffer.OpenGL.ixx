module;

#include <vector>
#include <numeric>
#include <glew/glew.h>

export module Rev.Graphics.VertexBuffer;

import Rev.Core.Vertex;

export namespace Rev::Graphics {

    using namespace Rev::Core;

    struct VertexBuffer {

        struct Params {

            size_t divisor = 0;
            size_t num = 0;

            std::vector<size_t> attribs;
        };

        Params params;

        // Track buffer
        GLuint vaoID = 0;
        GLuint bufferID = 0;

        // Buffer data and size
        void* data = nullptr;
        size_t size = 0;

        VertexBuffer(void* context, Params params) {

            this->params = params;

            glGenVertexArrays(1, &vaoID);
            glBindVertexArray(vaoID);

            this->params.num = 0;
            this->resize(params.num);
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

            // If no change, do nothing
            if (newNum == params.num) { return; }
            else { params.num = newNum; }

            // Derive vertex size, calculate buffer size
            size_t vertSize = sizeof(float) * std::accumulate(params.attribs.begin(), params.attribs.end(), 0);
            size = params.num * vertSize;
            
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

            size_t idx = 0, offset = 0;
            for (size_t attrib : params.attribs) {

                glVertexAttribPointer(idx, attrib, GL_FLOAT, GL_FALSE, vertSize, (void*)(offset * sizeof(float)));
                glEnableVertexAttribArray(idx);

                idx += 1;
                offset += attrib;
            }
        
            if (params.divisor) {
                glVertexAttribDivisor(0, params.divisor);
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