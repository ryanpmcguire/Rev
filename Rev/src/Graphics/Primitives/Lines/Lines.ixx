module;

#include <cmath>
#include <vector>
#include "./TriangulatePolyline.hpp"

export module Rev.Graphics.Lines;

import Rev.Graphics.Canvas;
import Rev.Graphics.Primitive;
import Rev.Graphics.UniformBuffer;
import Rev.Graphics.VertexBuffer;
import Rev.Graphics.Pipeline;
import Rev.Graphics.Shader;

// Shader file resources
import Resources.Shaders.OpenGL.Lines.Lines_vert;
import Resources.Shaders.OpenGL.Lines.Lines_frag;
import Resources.Shaders.Metal.Lines.Lines_metal;

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
                    .openGlFrag = Lines_frag,
                    .metalUniversal = Lines_metal
                }, 2, false);
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

        // We may own our points, or we may be given a pointer to some other points
        std::vector<Pos> points;
        std::vector<Pos>* pPoints = nullptr;

        size_t numSegments, numQuads, numJoins, numVerts;

        // Create
        Lines(Canvas* canvas, std::vector<Pos>* pPoints = nullptr) : Primitive(canvas) {

            if (pPoints) { this->pPoints = pPoints; }
            else { this->pPoints = &points; }

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
            delete databuff;
        }

        void compute() override {

            std::vector<Pos>& rPoints = *pPoints;

            numQuads = numSegments = rPoints.size() - 1;
            numJoins = numSegments - 1;
            numVerts = 6 * numQuads + 3 * numJoins;

            vertices->resize(numVerts);
            VertexBuffer::Vertex* verts = vertices->verts();
        
            int numTriangles = triangulatePolyline(
                reinterpret_cast<float*>(rPoints.data()), rPoints.size(),
                reinterpret_cast<float*>(verts), numVerts,
                data->strokeWidth
            );
        }

        void draw() override {

            shared.pipeline->bind();

            databuff->bind(1);
            vertices->bind();
            
            canvas->drawArrays(Pipeline::Topology::TriangleList, 0, numVerts);
        }
    };
};