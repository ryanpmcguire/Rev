module;

#include <cstddef>
#include <cstring>
#include <vector>

#include <sentinel.hpp>
#include <managed.hpp>

export module Rev.Primitive.FastLines3d;

import Rev.Primitive;
import Rev.Core.Resource;
import Rev.Core.Shared;
import Rev.Core.Color;
import Rev.Core.Vertex3;

import Rev.Graphics.Canvas;
import Rev.Graphics.Pipeline;
import Rev.Graphics.VertexBuffer;
import Rev.Graphics.UniformBuffer;
import Rev.Graphics.TextureBuffer;

export namespace Rev::Primitives {

    using namespace sentinel;
    using namespace Rev::Core;

    // FastLines, in 3D. The points carry depth; each is projected through the
    // camera (viewProj x world x model), but the on-screen stroke is still a
    // proper 2D polyline run through the same miter/bevel/fan triangulator as
    // FastLines -- so joins are clean and the stroke is a constant screen-space
    // width regardless of depth or zoom. The fragment shader reconstructs a
    // per-fragment depth and writes gl_FragDepth, so a thick line occludes (and
    // is occluded by) meshes in the same View3D, per pixel, across the joins.
    //
    // Drop-in sibling of Lines3d: bind the camera UBO at binding 2 (View3D does
    // this), set the per-actor world/model transforms with setTransforms(), set
    // the viewport (logical pixels) with setViewport(), then compute()/draw().
    // Points live in a TBO (two RG32F texels per point: (x,y) then (z,_)), and
    // each segment is one instance of a 15-vertex template.
    struct FastLines3d : public Primitive {

        // Shared
        //--------------------------------------------------

        inline static Shared shared;
        inline static Pipeline* pipeline = nullptr;
        inline static VertexBuffer* dummyVao = nullptr;   // core profile needs a bound VAO

        void createShared() {

            pipeline = new Pipeline(canvas->context, {
                .attribs = {},
                .openGlVert = File("./Shaders/FastLines3d.vert"),
                .openGlFrag = File("./Shaders/FastLines3d.frag"),
                .metalUniversal = File("./Shaders/FastLines3d.metal")
            });

            dummyVao = new VertexBuffer(canvas->context, { .num = 0, .divisor = 0, .attribs = {} });
        }

        void destroyShared() {
            delete pipeline;
            delete dummyVao;
        }

        // Per-line uniform block (std140: 48 bytes). Mirrors the shaders.
        struct Data {
            Color color;           // offset 0  (vec4)
            float strokeWidth;     // 16
            float smoothing;       // 20
            float pointCount;      // 24
            float opacity = 1.0f;  // 28
            float viewportX = 1.0f;// 32  (vec2 uViewport)
            float viewportY = 1.0f;// 36
            float pad0 = 0.0f;     // 40
            float pad1 = 0.0f;     // 44
        };

        // Per-actor transform stack (binding 3): world then model, column-major.
        // Same convention as Lines3d/Mesh3d; identity renders in place.
        static constexpr float Identity16[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };

        // Instance API (mirrors FastLines)
        //--------------------------------------------------

        Color color;
        float strokeWidth = 1.0f;
        float smoothing = 1.0f;
        float opacity = 1.0f;

        // Viewport in logical pixels (so the stroke width is in logical pixels).
        // The owner (e.g. an Actor) sets this from the View's canvas size.
        float viewportWidth = 1.0f;
        float viewportHeight = 1.0f;

        // compute() re-packs the points TBO and the per-line Data block. Points
        // are usually static, so gate the work: set this whenever the geometry,
        // colour, stroke, opacity, or viewport changes (the owner mutates a field
        // then sets dirty). The per-actor transform bypasses this -- it's written
        // straight into the mapped buffer in setTransforms() and stays live.
        bool dirty = true;

        struct Line {

            std::vector<Vertex3> points;
            std::vector<Vertex3>* pPoints = nullptr;

            Color color = { 1, 1, 1, 1 };
            float strokeWidth = -0.0f;
            float smoothing = -0.0f;

            std::vector<Vertex3>& getPoints() {
                if (pPoints) { return *pPoints; }
                return points;
            }
        };

        std::vector<Line> lines;

        // Per-line GPU resources, pooled and reused across frames.
        std::vector<TextureBuffer*> pointBuffers;
        std::vector<UniformBuffer*> dataBuffers;
        std::vector<size_t> instanceCounts;   // segments per line (= points - 1)

        // Per-actor world/model transforms (binding 3), persistently mapped.
        UniformBuffer* xformBuff = nullptr;
        float* xform = nullptr;   // [0..15] world, [16..31] model

        std::vector<float> scratch;           // xyz packing scratch

        // Create
        FastLines3d(Canvas* canvas, std::vector<std::vector<Vertex3>*> pLines = {}) : Primitive(canvas) {

            for (std::vector<Vertex3>*& pPoints : pLines) {
                lines.push_back({ .pPoints = pPoints });
            }

            shared.create([this]() { this->createShared(); });

            xformBuff = new UniformBuffer(canvas->context, sizeof(float) * 32);
            xform = (float*)xformBuff->data;
            std::memcpy(xform,      Identity16, sizeof(Identity16));
            std::memcpy(xform + 16, Identity16, sizeof(Identity16));
        }

        // Destroy
        ~FastLines3d() {

            for (TextureBuffer* buffer : pointBuffers) { delete buffer; }
            for (UniformBuffer* buffer : dataBuffers) { delete buffer; }

            delete xformBuff;

            shared.destroy([this]() { this->destroyShared(); });
        }

        // Set the per-actor world / model transforms (column-major 4x4 each).
        void setTransforms(const float* world16, const float* model16) {
            std::memcpy(xform,      world16, sizeof(float) * 16);
            std::memcpy(xform + 16, model16, sizeof(float) * 16);
        }

        // Set the viewport (logical pixels) used to convert the stroke width and
        // join geometry into screen space. Only re-packs on an actual change
        // (called every frame by the owner, but the size rarely moves).
        void setViewport(float width, float height) {
            if (viewportWidth == width && viewportHeight == height) { return; }
            viewportWidth = width;
            viewportHeight = height;
            dirty = true;
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

        // Upload: pack each line's points into its TBO and uniform block. Each
        // point becomes two RG32F texels -- (x, y) then (z, 0).
        void compute() override {

            if (!dirty) { return; }
            dirty = false;

            reservePool(lines.size());
            instanceCounts.assign(lines.size(), 0);

            for (size_t i = 0; i < lines.size(); i++) {

                std::vector<Vertex3>& pts = lines[i].getPoints();

                if (pts.size() < 2) { instanceCounts[i] = 0; continue; }

                scratch.resize(pts.size() * 4);
                for (size_t k = 0; k < pts.size(); k++) {
                    scratch[k * 4 + 0] = pts[k].x;
                    scratch[k * 4 + 1] = pts[k].y;
                    scratch[k * 4 + 2] = pts[k].z;
                    scratch[k * 4 + 3] = 0.0f;
                }

                // Two texels per point.
                pointBuffers[i]->set(scratch.data(), pts.size() * 2);

                Data block;
                block.color = color ? color : lines[i].color;
                block.strokeWidth = set(lines[i].strokeWidth) ? lines[i].strokeWidth : strokeWidth;
                block.smoothing = set(lines[i].smoothing) ? lines[i].smoothing : smoothing;
                block.pointCount = static_cast<float>(pts.size());
                block.opacity = opacity;
                block.viewportX = viewportWidth;
                block.viewportY = viewportHeight;

                dataBuffers[i]->set(&block);

                instanceCounts[i] = pts.size() - 1;
            }
        }

        // Draw: bind the per-actor transform once, then each line's prepared
        // buffers, and issue one instanced draw per line. The camera UBO is bound
        // by View3D at binding 2.
        void draw() override {

            if (instanceCounts.empty()) { return; }

            pipeline->bind();
            dummyVao->bind();

            xformBuff->bind(3);

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
