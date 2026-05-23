module;

#include <cmath>
#include <vector>

#include <managed.hpp>

export module Rev.Primitive.Mesh3d;

import Rev.Primitive;
import Rev.Core.Resource;
import Rev.Core.Shared;
import Rev.Core.Color;
import Rev.Core.Vertex3;

import Rev.Graphics.Canvas;
import Rev.Graphics.UniformBuffer;
import Rev.Graphics.VertexBuffer;
import Rev.Graphics.Pipeline;
import Rev.Graphics.Shader;

export namespace Rev::Primitives {

    struct Mesh3d : public Primitive {

        // Shared
        //--------------------------------------------------
        
        void createShared() {

            pipeline = new Pipeline(canvas->context, {

                .instanced = false,
                .attribs = Vertex3::attribs,

                .openGlVert = File("./Shaders/Mesh3d.vert"),
                .openGlFrag = File("./Shaders/Mesh3d.frag"),
                .metalUniversal = File("./Shaders/Mesh3d.metal")
            });
        }

        void destroyShared() {

            delete pipeline;
        }

        // Instance
        //--------------------------------------------------

        // Instance-specific data
        struct Data {
            Color color = { 1, 1, 1, 1 };
            float depth = 0.5f;
            float pad1 = 0.0f;
            float pad2 = 0.0f;
            float pad3 = 0.0f;
        };

        inline static Shared shared;
        inline static Pipeline* pipeline;

        UniformBuffer* databuff = nullptr;
        VertexBuffer* vertices = nullptr;

        Data* data = nullptr;
        bool dirty = true;

        Color color = { 1, 1, 1, 1 };

        // We may own our triangles, or we may be given a pointer
        // to some other triangle vertex list.
        std::vector<Vertex3> triangles;
        std::vector<Vertex3>* pTriangles = nullptr;

        size_t numFaces = 0;
        size_t numVerts = 0;

        struct Params {
            std::vector<Vertex3>* triangles = nullptr;
        };
        
        // Create
        Mesh3d(Canvas* canvas, Params params) : Primitive(canvas) {

            // Set params
            //--------------------------------------------------

            if (params.triangles) { pTriangles = params.triangles; }
            else { pTriangles = &triangles; }

            // Initialize resources
            //--------------------------------------------------
            
            // Create shared pipeline
            shared.create([this]() { this->createShared(); });

            vertices = new VertexBuffer(canvas->context, { .attribs = Vertex3::attribs });
            databuff = new UniformBuffer(canvas->context, sizeof(Data));
            data = (Data*)databuff->data;
        }

        // Destroy
        ~Mesh3d() {

            // Destroy shared pipeline
            shared.destroy([this]() { this->destroyShared(); });

            delete vertices;
            delete databuff;
        }

        std::vector<Vertex3>* getTriangles() {

            if (pTriangles) {
                return pTriangles;
            }

            return &triangles;
        }

        void compute() override {

            if (!dirty) { return; }

            dirty = false;

            data->color = color;
            data->depth = 0.5f;

            std::vector<Vertex3>* pSource = getTriangles();

            if (!pSource) {
                numVerts = 0;
                numFaces = 0;
                vertices->resize(0);
                return;
            }

            std::vector<Vertex3>& rTriangles = *pSource;

            // Each face is one triangle.
            numVerts = rTriangles.size();
            numFaces = numVerts / 3;

            vertices->resize(numVerts);

            Vertex3* verts = vertices->verts3();

            for (size_t i = 0; i < numVerts; i++) {
                verts[i] = rTriangles[i];
            }
        }

        void draw() override {

            pipeline->bind();

            databuff->bind(1);
            vertices->bind();
            
            canvas->drawArrays(Pipeline::Topology::TriangleList, 0, numVerts);
        }
    };
};