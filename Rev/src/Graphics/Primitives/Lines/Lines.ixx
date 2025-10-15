module;

#include <cmath>
#include <vector>
#include <glew/glew.h>
#include "./TriangulatePolyline.hpp"

export module Rev.Graphics.Lines;

import Rev.Graphics.Canvas;
import Rev.Graphics.Primitive;
import Rev.Graphics.UniformBuffer;
import Rev.Graphics.VertexBuffer;
import Rev.Graphics.Pipeline;
import Rev.Graphics.Shader;
import Resources.Shaders.OpenGL.Lines.Lines_vert;
import Resources.Shaders.OpenGL.Lines.Lines_frag;

import Rev.Pos;

export namespace Rev {

    struct Lines : public Primitive {

        // Shared across instances
        struct Shared {

            size_t refCount = 0;

            Pipeline* pipeline = nullptr;

            Shared() {
                
            }

            void create(Canvas* canvas) {

                refCount++;

                if (refCount > 1) { return; }

                pipeline = new Pipeline(canvas->context, {
                    .openGlVert = Lines_vert,
                    .openGlFrag = Lines_frag
                });
            }

            void destroy() {

                // Subtract refcount, return if remaining
                if (refCount--) { return; }

                // Delete resourcesfg
                delete pipeline;
            }
        };

        // Instance-specific data
        struct Data {

            struct Color { float r, g, b, a; };

            Color color = { 1, 1, 1, 1 };
            float strokeWidth = 1.0f;
            float miterLimit = 1.0f;
        };

        inline static Shared shared;
        UniformBuffer* databuff = nullptr;
        VertexBuffer* vertices = nullptr;

        Data* data = nullptr;
        bool dirty = true;
        
        size_t vertexCount, maxTriangles, maxVertices;

        std::vector<Pos> points;

        // Create
        Lines(Canvas* canvas) : Primitive(canvas) {

            shared.create(canvas);

            vertices = new VertexBuffer(canvas->context);
            databuff = new UniformBuffer(canvas->context, sizeof(Data));
            data = (Data*)databuff->data;
            *data = Data();
        }

        // Destroy
        ~Lines() {

            shared.destroy();

            delete vertices;
        }

        void compute() override {

            size_t num = 2 * 6 * (points.size() - 1);
            maxTriangles = 4 * num - 2;
            maxVertices = maxTriangles * 3;

            // Num points, 6 * num points for quads
            vertices->resize(num);
            VertexBuffer::Vertex* verts = vertices->verts();

            size_t vtx = 0;
            size_t count = points.size();
            if (count < 2) return;
        
            int numTriangles = triangulatePolyline(
                reinterpret_cast<float*>(points.data()),
                static_cast<int>(count),
                data->strokeWidth,
                reinterpret_cast<float*>(verts),
                maxTriangles
            );
            
            vertexCount = maxVertices;
        }

        void draw() override {

            shared.pipeline->bind();

            databuff->bind(1);
            vertices->bind();
            
            canvas->drawArrays(Pipeline::Topology::TriangleList, 0, vertexCount);
        }
    };
};