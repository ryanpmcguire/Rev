module;

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <limits>
#include <vector>

#include <managed.hpp>

export module Rev.Primitive.Mesh3d;

import Rev.Primitive;
import Rev.Core.Resource;
import Rev.Core.Shared;
import Rev.Core.Color;
import Rev.Core.Pos3;
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
            float opacity = 1.0f;
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

            xformBuff = new UniformBuffer(canvas->context, sizeof(float) * 32);
            xform = (float*)xformBuff->data;
            std::memcpy(xform,      Identity16, sizeof(Identity16));
            std::memcpy(xform + 16, Identity16, sizeof(Identity16));
        }

        // Destroy
        ~Mesh3d() {

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

        // Geometry
        //--------------------------------------------------

        static void AddCube(
            std::vector<Vertex3>& triangles,
            float size = 2.0f,
            Color color = { 0.72f, 0.76f, 0.80f, 1.0f }
        ) {

            auto addFace = [&triangles, color](
                Pos3 a,
                Pos3 b,
                Pos3 c,
                Pos3 d,
                Pos3 normal
            ) {

                triangles.push_back(Vertex3(a, color, normal));
                triangles.push_back(Vertex3(b, color, normal));
                triangles.push_back(Vertex3(c, color, normal));

                triangles.push_back(Vertex3(a, color, normal));
                triangles.push_back(Vertex3(c, color, normal));
                triangles.push_back(Vertex3(d, color, normal));
            };

            float s = size * 0.5f;

            addFace(
                { -s, -s,  s },
                {  s, -s,  s },
                {  s,  s,  s },
                { -s,  s,  s },
                {  0.0f,  0.0f,  1.0f }
            );

            addFace(
                {  s, -s, -s },
                { -s, -s, -s },
                { -s,  s, -s },
                {  s,  s, -s },
                {  0.0f,  0.0f, -1.0f }
            );

            addFace(
                { -s, -s, -s },
                { -s, -s,  s },
                { -s,  s,  s },
                { -s,  s, -s },
                { -1.0f,  0.0f,  0.0f }
            );

            addFace(
                {  s, -s,  s },
                {  s, -s, -s },
                {  s,  s, -s },
                {  s,  s,  s },
                {  1.0f,  0.0f,  0.0f }
            );

            addFace(
                { -s,  s,  s },
                {  s,  s,  s },
                {  s,  s, -s },
                { -s,  s, -s },
                {  0.0f,  1.0f,  0.0f }
            );

            addFace(
                { -s, -s, -s },
                {  s, -s, -s },
                {  s, -s,  s },
                { -s, -s,  s },
                {  0.0f, -1.0f,  0.0f }
            );
        }

        static Mesh3d* Cube(
            Canvas* canvas,
            float size = 2.0f,
            Color color = { 0.72f, 0.76f, 0.80f, 1.0f }
        ) {

            Mesh3d* mesh = new Mesh3d(canvas, {});

            AddCube(mesh->triangles, size, color);

            mesh->color = color;
            mesh->dirty = true;

            return mesh;
        }

        std::vector<Vertex3>* getTriangles() {

            if (pTriangles) {
                return pTriangles;
            }

            return &triangles;
        }

        // BVH acceleration structure
        //--------------------------------------------------

        struct BVHNode {
            float minX, minY, minZ;
            float maxX, maxY, maxZ;
            uint32_t leftOrFirst;  // internal: left child index (right = left+1); leaf: first tri in bvhTriIds
            uint32_t triCount;     // 0 = internal node, >0 = leaf
        };

        std::vector<BVHNode> bvhNodes;
        std::vector<uint32_t> bvhTriIds;
        uint32_t bvhNodeUsed = 0;
        bool bvhBuilt = false;

        bool hasAccel() const { return bvhBuilt; }

        static float triCentroid(const std::vector<Vertex3>& tris, uint32_t ti, int axis) {
            float v0 = axis == 0 ? tris[ti*3+0].x : axis == 1 ? tris[ti*3+0].y : tris[ti*3+0].z;
            float v1 = axis == 0 ? tris[ti*3+1].x : axis == 1 ? tris[ti*3+1].y : tris[ti*3+1].z;
            float v2 = axis == 0 ? tris[ti*3+2].x : axis == 1 ? tris[ti*3+2].y : tris[ti*3+2].z;
            return (v0 + v1 + v2) / 3.0f;
        }

        void computeNodeBounds(uint32_t idx, const std::vector<Vertex3>& tris) {
            BVHNode& n = bvhNodes[idx];
            n.minX = n.minY = n.minZ =  std::numeric_limits<float>::max();
            n.maxX = n.maxY = n.maxZ = -std::numeric_limits<float>::max();
            for (uint32_t i = 0; i < n.triCount; i++) {
                uint32_t ti = bvhTriIds[n.leftOrFirst + i];
                for (int v = 0; v < 3; v++) {
                    float x = tris[ti*3+v].x, y = tris[ti*3+v].y, z = tris[ti*3+v].z;
                    n.minX = std::min(n.minX, x); n.maxX = std::max(n.maxX, x);
                    n.minY = std::min(n.minY, y); n.maxY = std::max(n.maxY, y);
                    n.minZ = std::min(n.minZ, z); n.maxZ = std::max(n.maxZ, z);
                }
            }
        }

        void subdivideNode(uint32_t idx, std::vector<Vertex3>& tris) {
            uint32_t nodeFirst = bvhNodes[idx].leftOrFirst;
            uint32_t nodeCount = bvhNodes[idx].triCount;

            if (nodeCount <= 4) { return; }

            float ex = bvhNodes[idx].maxX - bvhNodes[idx].minX;
            float ey = bvhNodes[idx].maxY - bvhNodes[idx].minY;
            float ez = bvhNodes[idx].maxZ - bvhNodes[idx].minZ;
            int axis = (ex >= ey && ex >= ez) ? 0 : (ey >= ez) ? 1 : 2;

            float split = 0.0f;
            for (uint32_t i = 0; i < nodeCount; i++) {
                split += triCentroid(tris, bvhTriIds[nodeFirst + i], axis);
            }
            split /= (float)nodeCount;

            int lo = (int)nodeFirst, hi = (int)(nodeFirst + nodeCount - 1);
            while (lo <= hi) {
                if (triCentroid(tris, bvhTriIds[lo], axis) < split) {
                    lo++;
                } else {
                    std::swap(bvhTriIds[lo], bvhTriIds[hi--]);
                }
            }

            uint32_t leftCount = (uint32_t)lo - nodeFirst;
            if (leftCount == 0 || leftCount == nodeCount) { return; }

            uint32_t left  = bvhNodeUsed++;
            uint32_t right = bvhNodeUsed++;  // right = left + 1 guaranteed

            bvhNodes[left].leftOrFirst  = nodeFirst;
            bvhNodes[left].triCount     = leftCount;
            bvhNodes[right].leftOrFirst = nodeFirst + leftCount;
            bvhNodes[right].triCount    = nodeCount - leftCount;

            bvhNodes[idx].triCount    = 0;
            bvhNodes[idx].leftOrFirst = left;

            computeNodeBounds(left, tris);
            computeNodeBounds(right, tris);

            subdivideNode(left, tris);
            subdivideNode(right, tris);
        }

        void buildBVH() {
            bvhBuilt = false;
            bvhNodes.clear();
            bvhTriIds.clear();
            bvhNodeUsed = 0;

            std::vector<Vertex3>* pSrc = getTriangles();
            if (!pSrc || pSrc->size() < 3) { return; }

            std::vector<Vertex3>& tris = *pSrc;
            uint32_t triCount = (uint32_t)(tris.size() / 3);
            if (triCount == 0) { return; }

            bvhTriIds.resize(triCount);
            for (uint32_t i = 0; i < triCount; i++) { bvhTriIds[i] = i; }

            bvhNodes.resize(2 * triCount);
            bvhNodeUsed = 1;

            bvhNodes[0].leftOrFirst = 0;
            bvhNodes[0].triCount    = triCount;
            computeNodeBounds(0, tris);
            subdivideNode(0, tris);

            bvhBuilt = true;
        }

        static bool rayAABB(
            float ox, float oy, float oz,
            float dx, float dy, float dz,
            const BVHNode& n,
            float& outTNear
        ) {
            const float huge = std::numeric_limits<float>::max();
            const float eps  = 1e-8f;
            float tmin = -huge, tmax = huge;

            auto slab = [&](float o, float d, float bmin, float bmax) -> bool {
                if (std::fabs(d) < eps) { return o >= bmin && o <= bmax; }
                float t1 = (bmin - o) / d, t2 = (bmax - o) / d;
                if (t1 > t2) { std::swap(t1, t2); }
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
                return tmin <= tmax;
            };

            if (!slab(ox, dx, n.minX, n.maxX)) { return false; }
            if (!slab(oy, dy, n.minY, n.maxY)) { return false; }
            if (!slab(oz, dz, n.minZ, n.maxZ)) { return false; }

            outTNear = tmin;
            return tmax > 0.0f;
        }

        static bool rayTriBVH(
            float ox, float oy, float oz,
            float dx, float dy, float dz,
            float ax, float ay, float az,
            float bx, float by, float bz,
            float cx, float cy, float cz,
            float& t
        ) {
            const float eps = 1e-6f;
            float e1x = bx-ax, e1y = by-ay, e1z = bz-az;
            float e2x = cx-ax, e2y = cy-ay, e2z = cz-az;
            float hx = dy*e2z - dz*e2y, hy = dz*e2x - dx*e2z, hz = dx*e2y - dy*e2x;
            float det = e1x*hx + e1y*hy + e1z*hz;
            if (det > -eps && det < eps) { return false; }
            float inv = 1.0f / det;
            float sx = ox-ax, sy = oy-ay, sz = oz-az;
            float u = inv * (sx*hx + sy*hy + sz*hz);
            if (u < 0.0f || u > 1.0f) { return false; }
            float qx = sy*e1z - sz*e1y, qy = sz*e1x - sx*e1z, qz = sx*e1y - sy*e1x;
            float v = inv * (dx*qx + dy*qy + dz*qz);
            if (v < 0.0f || u + v > 1.0f) { return false; }
            t = inv * (e2x*qx + e2y*qy + e2z*qz);
            return t > eps;
        }

        bool hitTestBVH(
            float ox, float oy, float oz,
            float dx, float dy, float dz,
            float& outT,
            size_t& outTriId
        ) {
            if (!bvhBuilt) { buildBVH(); }
            if (!bvhBuilt) { return false; }

            std::vector<Vertex3>* pSrc = getTriangles();
            if (!pSrc) { return false; }
            const std::vector<Vertex3>& tris = *pSrc;

            float bestT = std::numeric_limits<float>::max();
            bool found = false;

            uint32_t stack[64];
            int sp = 0;
            stack[sp++] = 0;

            while (sp > 0) {
                const BVHNode& node = bvhNodes[stack[--sp]];

                float tNear;
                if (!rayAABB(ox, oy, oz, dx, dy, dz, node, tNear)) { continue; }
                if (tNear > bestT) { continue; }

                if (node.triCount > 0) {
                    for (uint32_t i = 0; i < node.triCount; i++) {
                        uint32_t ti = bvhTriIds[node.leftOrFirst + i];
                        if (ti * 3 + 2 >= tris.size()) { continue; }
                        float t;
                        if (rayTriBVH(
                            ox, oy, oz, dx, dy, dz,
                            tris[ti*3+0].x, tris[ti*3+0].y, tris[ti*3+0].z,
                            tris[ti*3+1].x, tris[ti*3+1].y, tris[ti*3+1].z,
                            tris[ti*3+2].x, tris[ti*3+2].y, tris[ti*3+2].z,
                            t
                        ) && t < bestT) {
                            bestT  = t;
                            outT   = t;
                            outTriId = ti;
                            found  = true;
                        }
                    }
                } else {
                    if (sp + 1 < 64) {
                        stack[sp++] = node.leftOrFirst;
                        stack[sp++] = node.leftOrFirst + 1;
                    }
                }
            }

            return found;
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
            xformBuff->bind(3);
            vertices->bind();

            canvas->drawArrays(Pipeline::Topology::TriangleList, 0, numVerts);
        }
    };


};
