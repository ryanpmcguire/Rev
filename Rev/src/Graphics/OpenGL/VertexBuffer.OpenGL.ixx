module;

#include <vector>
#include <numeric>
#include <glew/glew.h>

export module Rev.OpenGL.VertexBuffer;

import Rev.Vertex;

export namespace Rev {

    struct VertexBuffer {

        struct Props {

            size_t divisor = 0;
            size_t num = 0;

            std::vector<size_t> attribs;
        };

        GLuint vaoID = 0;
        GLuint bufferID = 0;

        void* data = nullptr;
        size_t size = 0;

        Props props;

        VertexBuffer(void* context, Props props) {

            this->props = props;

            glGenVertexArrays(1, &vaoID);
            glBindVertexArray(vaoID);

            this->resize(props.num);
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
            if (newNum == props.num) { return; }
            else { props.num = newNum; }

            size_t vertSize = sizeof(float) * std::accumulate(props.attribs.begin(), props.attribs.end(), 0);
            size = props.num * vertSize;
            
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
            for (size_t attrib : props.attribs) {

                glVertexAttribPointer(idx, attrib, GL_FLOAT, GL_FALSE, vertSize, (void*)(offset * sizeof(float)));
                glEnableVertexAttribArray(idx);

                idx += 1;
                offset += attrib;
            }
        
            if (props.divisor) {
                glVertexAttribDivisor(0, props.divisor);
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