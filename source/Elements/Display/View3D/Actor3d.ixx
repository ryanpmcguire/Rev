module;

#include <cstddef>
#include <cmath>
#include <vector>
#include <limits>
#include <string>

export module Rev.Element.View3d.Actor3d;

import Rev.Core.Color;
import Rev.Core.Pos3;
import Rev.Core.Vertex3;

import Rev.Appearance;
import Rev.Element;
import Rev.Element.Event;

import Rev.Primitive.Mesh3d;
import Rev.Primitive.FastLines3d;
import Rev.Graphics.Canvas;

export namespace Rev::Element::View3d {

    using namespace Rev::Appearance;

    struct Actor;

    namespace Styles {

        Style Actor3d = {
            .layout = { .position = Position::Absolute }
        };
    };

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

    struct Actor : public Rev::Element::Element {

        Primitives::Mesh3d* mesh = nullptr;
        Primitives::FastLines3d* lines = nullptr;

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

        // A transparent "ghost" actor must NOT write depth — otherwise its own faces occlude
        // each other inconsistently per view angle, and it hides geometry meant to read as
        // inside it (a toolpath, the tool). Draw ghosts AFTER the opaque actors so they blend
        // over them. Depth TEST stays on either way.
        bool writesDepth = true;

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
        // NOTE: bounds() still operates on raw (untransformed) vertices, so fit
        // assumes identity.  hitTest / hitTestVisible DO account for the
        // world × model transform (the ray is moved into the actor's local space
        // and the hit mapped back), so picking/orbit coincide with the on-screen
        // geometry even when the actor is transformed — tool preview, machine
        // simulation, execute pose.
        float worldTransform[16] = {
            1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1
        };
        float modelTransform[16] = {
            1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1
        };

        Actor(
            Rev::Element::Element* parent = nullptr,
            std::string name = "Actor3d"
        ) : Rev::Element::Element(parent, {}, name) {

            this->styles.prepend(&Styles::Actor3d);
        }

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

        virtual ~Actor() {

            if (ownsMesh) { delete mesh; }
            if (ownsLines) { delete lines; }

            mesh = nullptr;
            lines = nullptr;
        }

        // Initializers
        //--------------------------------------------------

        static Actor* Cube(
            Rev::Element::Element* parent,
            float size = 2.0f,
            Core::Color color = { 0.72f, 0.76f, 0.80f, 1.0f }
        ) {

            Actor* actor = new Actor(parent);

            actor->ownsMesh = true;

            if (actor->shared && actor->shared->canvas) {
                actor->mesh = Primitives::Mesh3d::Cube(
                    actor->shared->canvas,
                    size,
                    color
                );
            }

            return actor;
        }

        static Core::Pos3 vertexPos(const Core::Vertex3& v) {
            return {
                v.x,
                v.y,
                v.z
            };
        }

        // Transform helpers (column-major, matching the GPU convention)
        //--------------------------------------------------

        // out = a * b
        static void mul4(const float a[16], const float b[16], float out[16]) {
            for (int col = 0; col < 4; col++) {
                for (int row = 0; row < 4; row++) {
                    float sum = 0.0f;
                    for (int k = 0; k < 4; k++) {
                        sum += a[k * 4 + row] * b[col * 4 + k];
                    }
                    out[col * 4 + row] = sum;
                }
            }
        }

        static bool isIdentity(const float m[16]) {
            static const float I[16] = {
                1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1
            };
            for (int i = 0; i < 16; i++) {
                if (std::fabs(m[i] - I[i]) > 1e-6f) { return false; }
            }
            return true;
        }

        // Affine transform of a point (w = 1).
        static Core::Pos3 transformPoint(const float m[16], const Core::Pos3& p) {
            return {
                m[0] * p.x + m[4] * p.y + m[8]  * p.z + m[12],
                m[1] * p.x + m[5] * p.y + m[9]  * p.z + m[13],
                m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]
            };
        }

        // Affine transform of a direction (w = 0, no translation).
        static Core::Pos3 transformVec(const float m[16], const Core::Pos3& v) {
            return {
                m[0] * v.x + m[4] * v.y + m[8]  * v.z,
                m[1] * v.x + m[5] * v.y + m[9]  * v.z,
                m[2] * v.x + m[6] * v.y + m[10] * v.z
            };
        }

        // Inverse of an affine matrix (last row 0,0,0,1): inverts the upper 3x3
        // and the translation. Returns false if the linear part is singular.
        static bool affineInverse(const float m[16], float out[16]) {

            const float a = m[0], b = m[4], c = m[8];
            const float d = m[1], e = m[5], f = m[9];
            const float g = m[2], h = m[6], i = m[10];

            const float A =  (e * i - f * h);
            const float B = -(d * i - f * g);
            const float C =  (d * h - e * g);

            const float det = a * A + b * B + c * C;

            if (std::fabs(det) < 1e-12f) { return false; }

            const float invDet = 1.0f / det;

            const float inv00 = A * invDet;
            const float inv01 = -(b * i - c * h) * invDet;
            const float inv02 =  (b * f - c * e) * invDet;
            const float inv10 = B * invDet;
            const float inv11 =  (a * i - c * g) * invDet;
            const float inv12 = -(a * f - c * d) * invDet;
            const float inv20 = C * invDet;
            const float inv21 = -(a * h - b * g) * invDet;
            const float inv22 =  (a * e - b * d) * invDet;

            const float tx = m[12], ty = m[13], tz = m[14];

            out[0]  = inv00; out[1]  = inv10; out[2]  = inv20; out[3]  = 0.0f;
            out[4]  = inv01; out[5]  = inv11; out[6]  = inv21; out[7]  = 0.0f;
            out[8]  = inv02; out[9]  = inv12; out[10] = inv22; out[11] = 0.0f;
            out[12] = -(inv00 * tx + inv01 * ty + inv02 * tz);
            out[13] = -(inv10 * tx + inv11 * ty + inv12 * tz);
            out[14] = -(inv20 * tx + inv21 * ty + inv22 * tz);
            out[15] = 1.0f;

            return true;
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

        // The vertex attribute `a` carries a per-triangle pick group (the brep face id);
        // 0 ⇒ ungrouped. A face keeps one id across its vertices, so the first suffices.
        size_t faceIdOfTriangle(size_t triId) const {
            if (!mesh) { return 0; }
            std::vector<Core::Vertex3>* tris = mesh->getTriangles();
            if (!tris) { return 0; }
            size_t base = triId * 3;
            if (base >= tris->size()) { return 0; }
            return static_cast<size_t>((*tris)[base].a);
        }

        bool hitTestMesh(
            const Ray& ray,
            Hit& outHit
        ) {
            if (!mesh) { return false; }

            // The mesh is drawn through world × model, but its triangles live in
            // raw local space. Move the ray into that local space so the hittable
            // region coincides with the on-screen geometry, then map the hit back
            // to world. Identity transforms (the common edit-mode case) take the
            // raw path unchanged.
            float combined[16];
            mul4(worldTransform, modelTransform, combined);

            const bool transformed = !isIdentity(combined);

            Ray localRay = ray;
            float inv[16];

            if (transformed) {
                if (!affineInverse(combined, inv)) { return false; }
                localRay.origin    = transformPoint(inv, ray.origin);
                localRay.direction = transformVec(inv, ray.direction);
            }

            // Map a local-space hit (point/t) back into world space. Recomputes t
            // as the world distance along the world ray so View's nearest-hit
            // comparison stays consistent across actors with different transforms.
            auto mapHitBackToWorld = [&]() {
                if (!outHit.hit || !transformed) { return; }
                outHit.point = transformPoint(combined, outHit.point);
                const float dd = ray.direction.dot(ray.direction);
                outHit.t = (dd > 1e-12f)
                    ? (outHit.point - ray.origin).dot(ray.direction) / dd
                    : 0.0f;
            };

            if (mesh->hasAccel()) {
                float t; size_t triId;
                if (mesh->hitTestBVH(
                    localRay.origin.x, localRay.origin.y, localRay.origin.z,
                    localRay.direction.x, localRay.direction.y, localRay.direction.z,
                    t, triId
                )) {
                    outHit.hit      = true;
                    outHit.kind     = HitKind::Face;
                    outHit.actor    = this;
                    outHit.triangleId = triId;
                    outHit.faceId   = faceIdOfTriangle(triId);
                    outHit.point    = localRay.origin + localRay.direction * t;
                    outHit.t        = t;
                    mapHitBackToWorld();
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
                if (!rayTriangle(localRay, a, b, c, t)) { continue; }
                if (t < bestT) {
                    bestT = t;
                    outHit.hit = true;
                    outHit.kind = HitKind::Face;
                    outHit.actor = this;
                    outHit.triangleId = i / 3;
                    outHit.faceId = faceIdOfTriangle(i / 3);
                    outHit.point = localRay.origin + localRay.direction * t;
                    outHit.t = t;
                }
            }

            mapHitBackToWorld();

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
            return intersects(ray, outHit);
        }

        virtual bool intersects(
            const Ray& ray,
            Hit& outHit
        ) {
            return hitTestMesh(ray, outHit);
        }

        // The View ray-casts the cursor and delivers these to the actor under it. The
        // base does nothing; an actor overrides to react. `additive` ⇒ ctrl-click.
        virtual void onHover(const Hit& hit) {}
        virtual void onUnhover() {}
        virtual void onPick(const Hit& hit, bool additive) {}
        virtual void onDeselect() {}

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

                for (Primitives::FastLines3d::Line& ln : lines->lines) {
                    for (Core::Vertex3& v : ln.getPoints()) { include(v); }
                }
            }

            return valid;
        }

        // The logical (DPI-corrected) canvas size, fed to FastLines3d so its
        // stroke width is in logical pixels and its joins have the right aspect.
        // The 3D camera projects across the full canvas, so this matches.
        void updateLinesViewport() {

            if (!lines || !shared || !shared->canvas) { return; }

            float scale = shared->canvas->details.scale;
            float w = float(shared->canvas->details.width);
            float h = float(shared->canvas->details.height);

            if (scale > 0.0f) { w /= scale; h /= scale; }

            lines->setViewport(w, h);
        }

        void computePrimitives(Event& e) override {

            // Reflect the cascaded opacity into the primitives (parallel to Box).
            // Mark dirty on change so the new value is actually re-uploaded.
            if (mesh) {
                if (mesh->opacity != resolved.opacity) { mesh->opacity = resolved.opacity; mesh->dirty = true; }
                mesh->compute();
            }
            if (lines) {
                if (lines->opacity != resolved.opacity) { lines->opacity = resolved.opacity; lines->dirty = true; }
                updateLinesViewport();
                lines->compute();
            }
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

        // Drawn by the window's normal draw pass — so an actor anywhere in the
        // view's subtree draws itself, no View3d-managed list required. View3d
        // binds the camera and clears depth once before the actors run; each actor
        // turns depth testing on around its own geometry and off again (via the
        // canvas verbs), leaving the surrounding 2D pass's state untouched.
        void draw(Event& e) override {

            Element::draw(e);

            if (!visible) { return; }

            shared->canvas->depthTest(true);
            shared->canvas->depthWrite(writesDepth);

            draw();

            shared->canvas->depthTest(false);
            shared->canvas->depthWrite(true);   // restore for the next actor / the 2D pass
        }
    };
}
