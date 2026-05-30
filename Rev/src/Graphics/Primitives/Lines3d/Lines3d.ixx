module;

#include <cmath>
#include <cstring>
#include <vector>

#include <managed.hpp>

export module Rev.Primitive.Lines3d;

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

    struct Lines3d : public Primitive {

        // Shared
        //--------------------------------------------------
        
        void createShared() {

            pipeline = new Pipeline(canvas->context, {

                .instanced = false,
                .attribs = Vertex3::attribs,

                .openGlVert = File("./Shaders/Lines3d.vert"),
                .openGlFrag = File("./Shaders/Lines3d.frag"),
                .metalUniversal = File("./Shaders/Lines3d.metal")
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

        // Per-actor transform UBO (binding 3): world then model, column-major.
        // Persistently mapped — write directly, no dirty flag needed.
        static constexpr float Identity16[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };

        inline static Shared shared;
        inline static Pipeline* pipeline;

        UniformBuffer* databuff = nullptr;
        UniformBuffer* xformBuff = nullptr;
        VertexBuffer* vertices = nullptr;

        Data* data = nullptr;
        float* xform = nullptr;  // [0..15] world, [16..31] model
        bool dirty = true;

        Color color = { 1, 1, 1, 1 };

        // Every pair of vertices is one line segment.
        std::vector<Vertex3> lines;
        std::vector<Vertex3>* pLines = nullptr;

        size_t numLines = 0;
        size_t numVerts = 0;

        struct Params {
            std::vector<Vertex3>* lines = nullptr;
        };
        
        // Create
        Lines3d(Canvas* canvas, Params params) : Primitive(canvas) {

            // Set params
            //--------------------------------------------------

            if (params.lines) { pLines = params.lines; }
            else { pLines = &lines; }

            // Initialize resources
            //--------------------------------------------------
            
            // Create shared pipeline
            shared.create([this]() { this->createShared(); });

            vertices = new VertexBuffer(canvas->context, { .attribs = Vertex3::attribs });
            databuff = new UniformBuffer(canvas->context, sizeof(Data));
            data = (Data*)databuff->data;

            xformBuff = new UniformBuffer(canvas->context, sizeof(float) * 32);
            xform = (float*)xformBuff->data;
            std::memcpy(xform,      Identity16, sizeof(Identity16));
            std::memcpy(xform + 16, Identity16, sizeof(Identity16));
        }

        // Destroy
        ~Lines3d() {

            // Destroy shared pipeline
            shared.destroy([this]() { this->destroyShared(); });

            delete vertices;
            delete databuff;
            delete xformBuff;
        }

        // Set the per-actor world / model transforms (column-major 4x4 each).
        void setTransforms(const float* world16, const float* model16) {
            std::memcpy(xform,      world16, sizeof(float) * 16);
            std::memcpy(xform + 16, model16, sizeof(float) * 16);
        }

        std::vector<Vertex3>* getLines() {

            if (pLines) {
                return pLines;
            }

            return &lines;
        }

        void compute() override {

            if (!dirty) { return; }

            dirty = false;

            data->color = color;
            data->depth = 0.5f;

            std::vector<Vertex3>* pSource = getLines();

            if (!pSource) {
                numVerts = 0;
                numLines = 0;
                vertices->resize(0);
                return;
            }

            std::vector<Vertex3>& rLines = *pSource;

            // Every pair of vertices is one line.
            numVerts = rLines.size();
            numLines = numVerts / 2;

            vertices->resize(numVerts);

            Vertex3* verts = vertices->verts3();

            for (size_t i = 0; i < numVerts; i++) {
                verts[i] = rLines[i];
            }
        }

        void draw() override {

            pipeline->bind();

            databuff->bind(1);
            xformBuff->bind(3);
            vertices->bind();

            canvas->drawArrays(Pipeline::Topology::LineList, 0, numVerts);
        }
    };
};