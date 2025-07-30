module;

#include <cmath>
#include <vector>
#include <glew/glew.h>
#include "./Utils/TriangulatePolyline.hpp"

export module Rev.OpenGL.Lines;

import Rev.OpenGL.Primitive;
import Rev.OpenGL.VertexBuffer;
import Rev.OpenGL.Pipeline;
import Rev.OpenGL.Shader;
import Resources.Shaders.OpenGL.Lines.Lines_vert;
import Resources.Shaders.OpenGL.Lines.Lines_frag;

export namespace Rev {

    export struct Lines : public Primitive {

        // Shared across instances
        struct Shared {

            size_t refCount = 0;

            Shader* vert = nullptr;
            Shader* frag = nullptr;
            Pipeline* pipeline = nullptr;

            void create() {

                refCount++;

                // Create resources
                vert = new Shader(Lines_vert, GL_VERTEX_SHADER);
                frag = new Shader(Lines_frag, GL_FRAGMENT_SHADER);
                pipeline = new Pipeline(*(vert), *(frag));
            }

            void destroy() {

                // Subtract refcount, return if remaining
                if (refCount--) { return; }

                // Delete resources
                delete pipeline;
                delete vert;
                delete frag;
            }
        };

        inline static Shared shared;
        VertexBuffer* vertices = nullptr;
        bool dirty = true;

        struct Point { float x, y; };

        size_t num, vertexCount, maxTriangles, maxVertices;
        std::vector<Point> points;

        // Create
        Lines(size_t num) {

            this->num = num;

            shared.create();

            maxTriangles = 4 * num - 2;
            maxVertices = maxTriangles * 3;

            // Num points, 6 * num points for quads
            points.resize(num);
            vertices = new VertexBuffer(2 * 6 * (points.size() - 1));
        }

        // Destroy
        ~Lines() {

            shared.destroy();

            delete vertices;
        }

        void compute() {

            VertexBuffer::Vertex* verts = vertices->verts();
            size_t vtx = 0;
            const float strokeWidth = 50.f;       // total width (not half)
            const float miterLimit = 0.0f;
        
            size_t count = points.size();
            if (count < 2) return;
        
            int numTriangles = triangulatePolyline(
                reinterpret_cast<float*>(points.data()),
                static_cast<int>(count),
                strokeWidth,
                miterLimit,
                reinterpret_cast<float*>(verts),
                maxTriangles
            );
            
            vertexCount = maxVertices;
        }

        void draw() override {

            if (dirty) {
                this->compute();
                dirty = false;
            }

            shared.pipeline->bind();
            vertices->bind();
            glDrawArrays(GL_TRIANGLES, 0, vertexCount);
        }
    };
};