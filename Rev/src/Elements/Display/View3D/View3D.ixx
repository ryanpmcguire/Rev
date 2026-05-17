module;

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <limits>

#include <glew/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

export module Rev.Element.View3d;

import Rev.Core.Pos;
import Rev.Core.Vertex3;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;

import Rev.Graphics.Canvas;
import Rev.Graphics.UniformBuffer;

import Rev.Element.View3d.Actor3d;

export namespace Rev::Element {

    namespace Styles {
        
        Style View3d = {
            .overflow = Overflow::Hide,
            .size = { .width = Grow(), .height = Grow() },
            .margin = { 4_px, 4_px, 4_px, 4_px },
            .background = { .color = rgba(255, 255, 255, 0.05) }
        };
    };

    using namespace Core;

    struct View3d : public Box {

        // Camera UBO layout must match Mesh.vert / Mesh.frag.
        struct CameraData {
            glm::mat4 viewProj;
            glm::vec4 lightDir;
            glm::vec4 eyePos;
        };

        // View3d does NOT own these actors unless explicitly noted.
        std::vector<Rev::Actor3D*> actors;

        // Camera
        //--------------------------------------------------

        Graphics::UniformBuffer* cameraBuff = nullptr;

        glm::vec3 orbitPivot = { 0.0f, 0.0f, 0.0f };
        Core::Pos orbitMouse;
        bool hasOrbitPivot = false;

        Pos yawPitch = { 0.65f, 0.45f };
        Pos pinYawPitch;

        glm::vec3 target = { 0.0f, 0.0f, 0.0f };
        glm::vec3 pinTarget = { 0.0f, 0.0f, 0.0f };

        float distance = 4.0f;
        float orthoScale = 2.2f;

        // Create
        //--------------------------------------------------

        View3d(
            Element* parent,
            StyleList styles = {},
            std::string name = "View3d"
        ) : Box(parent, styles, "View3d") {

            // Self
            this->styles = { &Styles::View3d };

            Graphics::Canvas* canvas = shared->canvas;

            cameraBuff = new Graphics::UniformBuffer(
                canvas->context,
                sizeof(CameraData)
            );
        }

        ~View3d() {

            // View3d does not own arbitrary actors in actors.
            // It only owns the temporary demo cube actor.
            actors.clear();

            delete cameraBuff;
            cameraBuff = nullptr;
        }

        // Actor list
        //--------------------------------------------------

        void addActor(Rev::Actor3D* actor) {

            if (!actor) { return; }

            actors.push_back(actor);

            if (shared && shared->event) {
                refresh(*shared->event);
            }
        }

        void removeActor(Rev::Actor3D* actor) {

            actors.erase( std::remove(actors.begin(), actors.end(), actor), actors.end());

            if (shared && shared->event) {
                refresh(*shared->event); 
            }
        }

        void clearActors() {

            actors.clear();

            if (shared && shared->event) {
                refresh(*shared->event);
            }
        }

        // Camera
        //--------------------------------------------------

        glm::vec3 worldOnTargetPlane(Core::Pos mousePos, float scale) {

            glm::vec3 right;
            glm::vec3 up;
            glm::vec3 forward;

            cameraBasis(right, up, forward);

            float safeWidth = std::max(rect.w, 1.0f);
            float safeHeight = std::max(rect.h, 1.0f);
            float aspect = safeWidth / safeHeight;

            float localX = mousePos.x - rect.x;
            float localY = mousePos.y - rect.y;

            float ndcX = (localX / safeWidth) * 2.0f - 1.0f;
            float ndcY = 1.0f - (localY / safeHeight) * 2.0f;

            return (
                target
                + right * (ndcX * scale * aspect)
                + up * (ndcY * scale)
            );
        }

        glm::vec3 targetForScreenPoint(
            glm::vec3 worldPoint,
            Core::Pos mousePos,
            float scale
        ) {
            glm::vec3 right;
            glm::vec3 up;
            glm::vec3 forward;

            cameraBasis(right, up, forward);

            float safeWidth = std::max(rect.w, 1.0f);
            float safeHeight = std::max(rect.h, 1.0f);
            float aspect = safeWidth / safeHeight;

            float localX = mousePos.x - rect.x;
            float localY = mousePos.y - rect.y;

            float ndcX = (localX / safeWidth) * 2.0f - 1.0f;
            float ndcY = 1.0f - (localY / safeHeight) * 2.0f;

            return (
                worldPoint
                - right * (ndcX * scale * aspect)
                - up * (ndcY * scale)
            );
        }

        void rayFromMouse(
            Core::Pos mousePos,
            glm::vec3& rayOrigin,
            glm::vec3& rayDir
        ) {
            glm::vec3 right;
            glm::vec3 up;
            glm::vec3 forward;

            cameraBasis(right, up, forward);

            glm::vec3 planePoint = worldOnTargetPlane(
                mousePos,
                orthoScale
            );

            rayOrigin = planePoint - forward * distance;
            rayDir = forward;
        }

        bool rayTriangle(
            glm::vec3 origin,
            glm::vec3 dir,
            glm::vec3 a,
            glm::vec3 b,
            glm::vec3 c,
            float& t
        ) {
            const float eps = 1e-6f;

            glm::vec3 edge1 = b - a;
            glm::vec3 edge2 = c - a;

            glm::vec3 h = glm::cross(dir, edge2);
            float det = glm::dot(edge1, h);

            if (det > -eps && det < eps) {
                return false;
            }

            float invDet = 1.0f / det;

            glm::vec3 s = origin - a;
            float u = invDet * glm::dot(s, h);

            if (u < 0.0f || u > 1.0f) {
                return false;
            }

            glm::vec3 q = glm::cross(s, edge1);
            float v = invDet * glm::dot(dir, q);

            if (v < 0.0f || u + v > 1.0f) {
                return false;
            }

            t = invDet * glm::dot(edge2, q);

            return t > eps;
        }

        bool hitTest(
            Core::Pos mousePos,
            glm::vec3& hitPoint
        ) {
            glm::vec3 rayOrigin;
            glm::vec3 rayDir;

            rayFromMouse(
                mousePos,
                rayOrigin,
                rayDir
            );

            bool hit = false;
            float bestT = std::numeric_limits<float>::max();

            for (Rev::Actor3D* actor : actors) {

                if (!actor || !actor->visible) {
                    continue;
                }

                std::vector<Core::Vertex3>& triangles = actor->triangles;

                for (size_t i = 0; i + 2 < triangles.size(); i += 3) {

                    Core::Vertex3& va = triangles[i];
                    Core::Vertex3& vb = triangles[i + 1];
                    Core::Vertex3& vc = triangles[i + 2];

                    glm::vec3 a = { va.x, va.y, va.z };
                    glm::vec3 b = { vb.x, vb.y, vb.z };
                    glm::vec3 c = { vc.x, vc.y, vc.z };

                    float t = 0.0f;

                    if (!rayTriangle(rayOrigin, rayDir, a, b, c, t)) {
                        continue;
                    }

                    if (t < bestT) {
                        bestT = t;
                        hit = true;
                    }
                }
            }

            if (!hit) {
                return false;
            }

            hitPoint = rayOrigin + rayDir * bestT;

            return true;
        }

        glm::mat4 cameraRotation() {

            glm::mat4 r = glm::mat4(1.0f);

            r = glm::rotate(
                r,
                yawPitch.x,
                glm::vec3(0.0f, 1.0f, 0.0f)
            );

            r = glm::rotate(
                r,
                yawPitch.y,
                glm::vec3(1.0f, 0.0f, 0.0f)
            );

            return r;
        }

        void cameraBasis(
            glm::vec3& right,
            glm::vec3& up,
            glm::vec3& forward
        ) {
            glm::mat4 rot = cameraRotation();

            right = glm::normalize(
                glm::vec3(rot * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f))
            );

            up = glm::normalize(
                glm::vec3(rot * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f))
            );

            forward = glm::normalize(
                glm::vec3(rot * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f))
            );
        }

        void updateCamera() {

            float safeHeight = std::max(rect.h, 1.0f);
            float aspect = rect.w / safeHeight;

            glm::vec3 right;
            glm::vec3 up;
            glm::vec3 forward;

            cameraBasis(
                right,
                up,
                forward
            );

            glm::vec3 eye = target - forward * distance;

            glm::mat4 view = glm::lookAt(
                eye,
                target,
                up
            );

            glm::mat4 proj = glm::ortho(
                -orthoScale * aspect,
                orthoScale * aspect,
                -orthoScale,
                orthoScale,
                -100.0f,
                100.0f
            );

            CameraData data;

            data.viewProj = proj * view;

            glm::vec3 light = glm::normalize(
                glm::vec3(-0.4f, 0.8f, 0.6f)
            );

            data.lightDir = {
                light.x,
                light.y,
                light.z,
                0.0f
            };

            data.eyePos = {
                eye.x,
                eye.y,
                eye.z,
                1.0f
            };

            cameraBuff->set(
                &data
            );
        }

        // Events
        //--------------------------------------------------

        void mouseDown(Event& e) override {

            pinYawPitch = yawPitch;
            pinTarget = target;

            orbitMouse = e.mouse.pos;
            hasOrbitPivot = hitTest(
                e.mouse.pos,
                orbitPivot
            );

            if (!hasOrbitPivot) {
                orbitPivot = worldOnTargetPlane(
                    e.mouse.pos,
                    orthoScale
                );

                hasOrbitPivot = true;
            }

            Box::mouseDown(e);
        }

        void mouseDrag(Event& e) override {

            float rotateSensitivity = 0.008f;

            if (e.keyboard.shift) {

                glm::vec3 right;
                glm::vec3 up;
                glm::vec3 forward;

                cameraBasis(
                    right,
                    up,
                    forward
                );

                float worldPerPixel = (
                    2.0f * orthoScale
                ) / std::max(rect.h, 1.0f);

                glm::vec3 pan =
                    (-right * e.mouse.diff.x + up * e.mouse.diff.y)
                    * worldPerPixel;

                target = pinTarget + pan;
            }

            else {

                yawPitch.x = pinYawPitch.x - e.mouse.diff.x * rotateSensitivity;
                yawPitch.y = pinYawPitch.y - e.mouse.diff.y * rotateSensitivity;

                if (hasOrbitPivot) {
                    target = targetForScreenPoint(
                        orbitPivot,
                        orbitMouse,
                        orthoScale
                    );
                }
            }

            refresh(e);

            Box::mouseDrag(e);
        }

        void mouseWheel(Event& e) override {

            glm::vec3 before = worldOnTargetPlane(
                e.mouse.pos,
                orthoScale
            );

            float zoom = (
                e.mouse.wheel.y > 0.0f
                ? 0.9f
                : 1.1f
            );

            orthoScale *= zoom;

            orthoScale = std::clamp(
                orthoScale,
                0.05f,
                100.0f
            );

            glm::vec3 after = worldOnTargetPlane(
                e.mouse.pos,
                orthoScale
            );

            target += before - after;

            refresh(e);

            Box::mouseWheel(e);
        }

        // Computing
        //--------------------------------------------------

        void computePrimitives(Event& e) override {

            for (Rev::Actor3D* actor : actors) {

                if (!actor) {
                    continue;
                }

                actor->compute();
            }

            Box::computePrimitives(e);
        }

        // Draw
        //--------------------------------------------------

        void draw(Event& e) override {

            Box::draw(e);

            updateCamera();

            cameraBuff->bind(2);

            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_TRUE);

            glClearDepth(1.0);
            glClear(GL_DEPTH_BUFFER_BIT);

            for (Rev::Actor3D* actor : actors) {

                if (!actor) {
                    continue;
                }

                actor->draw();
            }

            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
        }
    };
}