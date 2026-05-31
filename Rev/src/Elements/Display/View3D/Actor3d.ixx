module;

#include <cstddef>
#include <vector>
#include <limits>

export module Rev.Element.View3d.Actor3d;

import Rev.Core.Pos3;
import Rev.Core.Vertex3;

import Rev.Primitive.Mesh3d;
import Rev.Primitive.Lines3d;

export namespace Rev::Element::View3d {

    struct Actor;

    enum class HitKind {
        None,
        Face,
        Edge,
        Vertex,
        Actor
    };

    struct Ray {
        Core::Pos3 origin = { 0.0f, 0.0f, 0.0f };
        Core::Pos3 direction = { 0.0f, 0.0f, -1.0f };
    };

    struct Hit {

        bool hit = false;

        HitKind kind = HitKind::None;

        Actor* actor = nullptr;

        size_t triangleId = 0;
        size_t faceId = 0;
        size_t edgeId = 0;

        Core::Pos3 point = { 0.0f, 0.0f, 0.0f };

        float t = 0.0f;
    };

    struct Actor {

        Primitives::Mesh3d* mesh = nullptr;
        Primitives::Lines3d* lines = nullptr;

        // Drawing and picking are intentionally separate.
        //
        // visible:
        //     Actor is drawn.
        //
        // selectable:
        //     Actor participates in View3d hit testing.
        bool visible = true;
        bool selectable = true;

        bool includeInFit = true;

        bool ownsMesh = false;
        bool ownsLines = false;
        bool ownsTriangles = false;

        // World-space transform applied to this actor's geometry on the GPU
        // (column-major 4x4).  Identity by default — the actor renders exactly
        // where its vertices sit.  Used e.g. for machine simulation, where the
        // whole part rotates rigidly.  modelTransform is a second local level
        // (also identity by default), giving a three-level stack with the
        // camera: viewProj × world × model.
        //
        // NOTE: hitTest / bounds operate on raw (untransformed) vertices, so
        // picking and fit assume identity.  This is fine: picking only happens
        // in edit mode where the transform is identity.
        float worldTransform[16] = {
            1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1
        };
        float modelTransform[16] = {
            1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1
        };

        void setWorldTransform(const float* m16) {
            for (int i = 0; i < 16; i++) { worldTransform[i] = m16[i]; }
        }

        void resetTransform() {
            static const float I[16] = {
                1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1
            };
            setWorldTransform(I);
            for (int i = 0; i < 16; i++) { modelTransform[i] = I[i]; }
        }

        ~Actor() {

            if (ownsMesh) { delete mesh; }
            if (ownsLines) { delete lines; }

            mesh = nullptr;
            lines = nullptr;
        }

        static Core::Pos3 vertexPos(const Core::Vertex3& v) {
            return {
                v.x,
                v.y,
                v.z
            };
        }

        static bool rayTriangle(
            const Ray& ray,
            Core::Pos3 a,
            Core::Pos3 b,
            Core::Pos3 c,
            float& t
        ) {
            // Moller-Trumbore intersection.
            //
            // Preserves the previous behavior:
            // - Two-sided triangle hit test.
            // - Reject nearly parallel rays.
            // - Reject hits behind the ray origin.
            const float eps = 1e-6f;

            Core::Pos3 edge1 = b - a;
            Core::Pos3 edge2 = c - a;

            Core::Pos3 h = ray.direction.cross(edge2);
            float det = edge1.dot(h);

            if (det > -eps && det < eps) { return false; }

            float invDet = 1.0f / det;

            Core::Pos3 s = ray.origin - a;
            float u = invDet * s.dot(h);

            if (u < 0.0f || u > 1.0f) { return false; }

            Core::Pos3 q = s.cross(edge1);
            float v = invDet * ray.direction.dot(q);

            if (v < 0.0f || u + v > 1.0f) { return false; }

            t = invDet * edge2.dot(q);

            return t > eps;
        }

        bool hitTestMesh(
            const Ray& ray,
            Hit& outHit
        ) {
            if (!mesh) { return false; }

            if (mesh->hasAccel()) {
                float t; size_t triId;
                if (mesh->hitTestBVH(
                    ray.origin.x, ray.origin.y, ray.origin.z,
                    ray.direction.x, ray.direction.y, ray.direction.z,
                    t, triId
                )) {
                    outHit.hit      = true;
                    outHit.kind     = HitKind::Face;
                    outHit.actor    = this;
                    outHit.triangleId = triId;
                    outHit.point    = ray.origin + ray.direction * t;
                    outHit.t        = t;
                    return true;
                }
                return false;
            }

            std::vector<Core::Vertex3>* pTriangles = mesh->getTriangles();
            if (!pTriangles) { return false; }
            std::vector<Core::Vertex3>& triangles = *pTriangles;

            float bestT = std::numeric_limits<float>::max();

            for (size_t i = 0; i + 2 < triangles.size(); i += 3) {
                Core::Pos3 a = vertexPos(triangles[i]);
                Core::Pos3 b = vertexPos(triangles[i + 1]);
                Core::Pos3 c = vertexPos(triangles[i + 2]);
                float t = 0.0f;
                if (!rayTriangle(ray, a, b, c, t)) { continue; }
                if (t < bestT) {
                    bestT = t;
                    outHit.hit = true;
                    outHit.kind = HitKind::Face;
                    outHit.actor = this;
                    outHit.triangleId = i / 3;
                    outHit.point = ray.origin + ray.direction * t;
                    outHit.t = t;
                }
            }

            return outHit.hit;
        }

        // Orbit-pivot hit test: gates on visible rather than selectable so the
        // pivot always lands on the geometry the user can actually see.
        bool hitTestVisible(
            const Ray& ray,
            Hit& outHit
        ) {
            outHit = Hit();
            if (!visible) { return false; }
            return hitTestMesh(ray, outHit);
        }

        bool hitTest(
            const Ray& ray,
            Hit& outHit
        ) {
            outHit = Hit();
            if (!selectable) { return false; }
            return hitTestMesh(ray, outHit);
        }

        bool bounds(
            Core::Pos3& min,
            Core::Pos3& max
        ) {
            if (!includeInFit) { return false; }

            bool valid = false;

            auto include = [&](Core::Vertex3& v) {

                Core::Pos3 p = vertexPos(v);

                if (!valid) {
                    min = p;
                    max = p;
                    valid = true;
                    return;
                }

                min = Core::Pos3::min(min, p);
                max = Core::Pos3::max(max, p);
            };

            if (mesh) {

                std::vector<Core::Vertex3>* pTriangles = mesh->getTriangles();

                if (pTriangles) {
                    for (Core::Vertex3& v : *pTriangles) { include(v); }
                }
            }

            if (lines) {

                std::vector<Core::Vertex3>* pLines = lines->getLines();

                if (pLines) {
                    for (Core::Vertex3& v : *pLines) { include(v); }
                }
            }

            return valid;
        }

        void compute() {
            if (mesh) { mesh->compute(); }
            if (lines) { lines->compute(); }
        }

        void draw() {
            if (!visible) { return; }

            if (mesh) {
                mesh->setTransforms(worldTransform, modelTransform);
                mesh->draw();
            }

            if (lines) {
                lines->setTransforms(worldTransform, modelTransform);
                lines->draw();
            }
        }
    };
}
