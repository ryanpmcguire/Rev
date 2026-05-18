module;

#include <cstddef>
#include <vector>
#include <limits>

#include <glm/glm.hpp>

export module Rev.Element.View3d.Actor3d;

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
        glm::vec3 origin = { 0.0f, 0.0f, 0.0f };
        glm::vec3 direction = { 0.0f, 0.0f, -1.0f };
    };

    struct Hit {

        bool hit = false;

        HitKind kind = HitKind::None;

        Actor* actor = nullptr;

        size_t triangleId = 0;
        size_t faceId = 0;
        size_t edgeId = 0;

        glm::vec3 point = { 0.0f, 0.0f, 0.0f };

        float t = 0.0f;
    };

    struct Actor {

        Primitives::Mesh3d* mesh = nullptr;
        Primitives::Lines3d* lines = nullptr;

        bool visible = true;
        bool includeInFit = true;

        bool ownsMesh = false;
        bool ownsLines = false;
        bool ownsTriangles = false;

        // Later:
        // glm::mat4 transform = glm::mat4(1.0f);
        // bool selected = false;
        // std::string name;
        // uint32_t id = 0;

        ~Actor() {

            if (ownsMesh) { delete mesh; }
            if (ownsLines) { delete lines; }

            mesh = nullptr;
            lines = nullptr;
        }

        bool bounds(glm::vec3& min, glm::vec3& max) {

            bool valid = false;

            auto include = [&](Core::Vertex3& v) {

                glm::vec3 p = { v.x, v.y, v.z };

                if (!valid) {
                    min = p;
                    max = p;
                    valid = true;
                    return;
                }

                min = glm::min(min, p);
                max = glm::max(max, p);
            };

            if (mesh) {

                std::vector<Core::Vertex3>* triangles = mesh->getTriangles();

                if (triangles) {
                    for (Core::Vertex3& v : *triangles) { include(v); }
                }
            }

            if (lines) {

                std::vector<Core::Vertex3>* lineVerts = lines->getLines();

                if (lineVerts) {
                    for (Core::Vertex3& v : *lineVerts) { include(v); }
                }
            }

            return valid;
        }

        static bool rayTriangle(
            const Ray& ray,
            glm::vec3 a,
            glm::vec3 b,
            glm::vec3 c,
            float& t
        ) {
            const float eps = 1e-6f;

            glm::vec3 edge1 = b - a;
            glm::vec3 edge2 = c - a;

            glm::vec3 h = glm::cross(ray.direction, edge2);
            float det = glm::dot(edge1, h);

            if (det > -eps && det < eps) { return false; }

            float invDet = 1.0f / det;

            glm::vec3 s = ray.origin - a;
            float u = invDet * glm::dot(s, h);

            if (u < 0.0f || u > 1.0f) { return false; }

            glm::vec3 q = glm::cross(s, edge1);
            float v = invDet * glm::dot(ray.direction, q);

            if (v < 0.0f || u + v > 1.0f) { return false; }

            t = invDet * glm::dot(edge2, q);

            return t > eps;
        }

        bool hitTest(
            const Ray& ray,
            Hit& outHit
        ) {
            outHit = Hit();

            if (!visible) { return false; }
            if (!mesh) { return false; }

            std::vector<Core::Vertex3>* pTriangles = mesh->getTriangles();

            if (!pTriangles) { return false; }

            std::vector<Core::Vertex3>& triangles = *pTriangles;

            float bestT = std::numeric_limits<float>::max();

            for (size_t i = 0; i + 2 < triangles.size(); i += 3) {

                Core::Vertex3& va = triangles[i];
                Core::Vertex3& vb = triangles[i + 1];
                Core::Vertex3& vc = triangles[i + 2];

                glm::vec3 a = { va.x, va.y, va.z };
                glm::vec3 b = { vb.x, vb.y, vb.z };
                glm::vec3 c = { vc.x, vc.y, vc.z };

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

        void compute() {
            if (mesh) { mesh->compute(); }
            if (lines) { lines->compute(); }
        }

        void draw() {
            if (!visible) { return; }
            if (mesh) { mesh->draw(); }
            if (lines) { lines->draw(); }
        }
    };
}