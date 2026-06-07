module;

#include <cstddef>
#include <vector>

#include <sentinel.hpp>
#include <managed.hpp>

export module Rev.Primitive.FastLines;

import Rev.Primitive;
import Rev.Core.Resource;
import Rev.Core.Shared;
import Rev.Core.Color;
import Rev.Core.Vertex;
import Rev.Core.Rect;

import Rev.Graphics.Canvas;
import Rev.Graphics.Pipeline;
import Rev.Graphics.VertexBuffer;
import Rev.Graphics.UniformBuffer;
import Rev.Graphics.TextureBuffer;

export namespace Rev::Primitives {

    using namespace sentinel;
    using namespace Rev::Core;

    // A drop-in-API replacement for Lines that triangulates the polyline on the
    // GPU. Points live in a TBO (one vec2 per point); each segment is one
    // instance of a 15-vertex template (a body trapezoid + a 3-triangle corner
    // fan) and the vertex shader builds the quad + miter joins. Upload happens in
    // compute(); draw() only binds and issues the instanced draw. The CPU-side
    // triangulator in Lines goes away once this matures (packed single-draw,
    // per-vertex colour).
    struct FastLines : public Primitive {

        // Shared
        //--------------------------------------------------

        inline static Shared shared;
        inline static Pipeline* pipeline = nullptr;
        inline static VertexBuffer* dummyVao = nullptr;   // core profile needs a bound VAO

        void createShared() {

            pipeline = new Pipeline(canvas->context, {
                .attribs = {},
                .openGlVert = File("./Shaders/FastLines.vert"),
                .openGlFrag = File("./Shaders/FastLines.frag"),
                .metalUniversal = File("./Shaders/FastLines.metal")
            });

            dummyVao = new VertexBuffer(canvas->context, { .num = 0, .divisor = 0, .attribs = {} });
        }

        void destroyShared() {
            delete pipeline;
            delete dummyVao;
        }

        // Uniform block (std140: 32 bytes)
        struct Data {
            Color color;           // offset 0
            float strokeWidth;     // 16
            float smoothing;       // 20
            float pointCount;      // 24
            float pad;             // 28
        };

        // Instance API (mirrors Lines)
        //--------------------------------------------------

        Color color;
        float strokeWidth = 1.0f;
        float smoothing = 1.0f;

        struct Line {

            std::vector<Vertex> points;
            std::vector<Vertex>* pPoints = nullptr;

            Color color = { 1, 1, 1, 1 };
            float strokeWidth = -0.0f;
            float smoothing = -0.0f;

            std::vector<Vertex>& getPoints() {
                if (pPoints) { return *pPoints; }
                return points;
            }
        };

        std::vector<Line> lines;

        // Per-line GPU resources, pooled and reused across frames. Each line owns
        // its own points TBO and uniform block so draw() can bind stable buffers
        // (no cross-draw mutation, no per-draw upload).
        std::vector<TextureBuffer*> pointBuffers;
        std::vector<UniformBuffer*> dataBuffers;
        std::vector<size_t> instanceCounts;   // segments per line (= points - 1)

        std::vector<float> scratch;           // xy packing scratch

        // Create
        FastLines(Canvas* canvas, std::vector<std::vector<Vertex>*> pLines = {}) : Primitive(canvas) {

            for (std::vector<Vertex>*& pPoints : pLines) {
                lines.push_back({ .pPoints = pPoints });
            }

            shared.create([this]() { this->createShared(); });
        }

        // Destroy
        ~FastLines() {

            for (TextureBuffer* buffer : pointBuffers) { delete buffer; }
            for (UniformBuffer* buffer : dataBuffers) { delete buffer; }

            shared.destroy([this]() { this->destroyShared(); });
        }

        // Grow the resource pool to cover at least `n` lines (never shrinks).
        void reservePool(size_t n) {

            while (pointBuffers.size() < n) {
                pointBuffers.push_back(new TextureBuffer(canvas->context));
            }

            while (dataBuffers.size() < n) {
                dataBuffers.push_back(new UniformBuffer(canvas->context, sizeof(Data)));
            }
        }

        // Upload: pack each line's points into its TBO and uniform block.
        void compute() override {

            reservePool(lines.size());
            instanceCounts.assign(lines.size(), 0);

            for (size_t i = 0; i < lines.size(); i++) {

                std::vector<Vertex>& pts = lines[i].getPoints();

                if (pts.size() < 2) { instanceCounts[i] = 0; continue; }

                scratch.resize(pts.size() * 2);
                for (size_t k = 0; k < pts.size(); k++) {
                    scratch[k * 2 + 0] = pts[k].x;
                    scratch[k * 2 + 1] = pts[k].y;
                }

                pointBuffers[i]->set(scratch.data(), pts.size());

                Data block;
                block.color = color ? color : lines[i].color;
                block.strokeWidth = set(lines[i].strokeWidth) ? lines[i].strokeWidth : strokeWidth;
                block.smoothing = set(lines[i].smoothing) ? lines[i].smoothing : smoothing;
                block.pointCount = static_cast<float>(pts.size());
                block.pad = 0.0f;

                dataBuffers[i]->set(&block);

                instanceCounts[i] = pts.size() - 1;
            }
        }

        // Draw: bind each line's prepared buffers and issue one instanced draw.
        void draw() override {

            if (instanceCounts.empty()) { return; }

            pipeline->bind();
            dummyVao->bind();

            for (size_t i = 0; i < instanceCounts.size(); i++) {

                if (instanceCounts[i] < 1) { continue; }

                pointBuffers[i]->bind(0);   // samplerBuffer at unit 0
                dataBuffers[i]->bind(1);    // uniform block at binding 1

                canvas->drawArraysInstanced(
                    Pipeline::Topology::TriangleList, 0, 15, instanceCounts[i]
                );
            }
        }
    };
};
