module;

#include <cmath>
#include <vector>
#include <glew/glew.h>
#include "./TriangulatePolyline.hpp"

export module Rev.Graphics.Lines;

import Rev.Graphics.Canvas;
import Rev.Graphics.Primitive;
import Rev.Graphics.VertexBuffer;
import Rev.Graphics.Pipeline;
import Rev.Graphics.Shader;
import Resources.Shaders.OpenGL.Lines.Lines_vert;
import Resources.Shaders.OpenGL.Lines.Lines_frag;

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

                pipeline = new Pipeline(canvas->context, {});
            }

            void destroy() {

                // Subtract refcount, return if remaining
                if (refCount--) { return; }

                // Delete resources
                delete pipeline;
            }
        };

        inline static Shared shared;
        VertexBuffer* vertices = nullptr;
        bool dirty = true;

        struct Point { float x, y; };

        size_t num, vertexCount, maxTriangles, maxVertices;
        std::vector<Point> points;

        // Create
        Lines(Canvas* canvas, size_t num) : Primitive(canvas) {

            this->num = num;

            shared.create(canvas);

            maxTriangles = 4 * num - 2;
            maxVertices = maxTriangles * 3;

            // Num points, 6 * num points for quads
            points.resize(num);
            vertices = new VertexBuffer(canvas->context, 2 * 6 * (points.size() - 1));
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

        void draw(Canvas* canvas) override {

            if (dirty) {
                this->compute();
                dirty = false;
            }

            shared.pipeline->bind(canvas->context);
            vertices->bind(canvas->context);
            
            canvas->drawArrays(Pipeline::Topology::TriangleList, 0, vertexCount);
        }
    };
};