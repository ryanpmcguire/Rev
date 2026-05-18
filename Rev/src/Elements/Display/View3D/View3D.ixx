module;

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <limits>

#define GLM_ENABLE_EXPERIMENTAL

#include <glew/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

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
            .border = { .color = rgba(0, 0, 0, 1), .width = 1_px },
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

        struct Hit {
            bool hit = false;

            Rev::Actor3D* actor = nullptr;

            size_t triangleId = 0;

            glm::vec3 point = { 0.0f, 0.0f, 0.0f };

            float t = 0.0f;
        };

        // View3d does NOT own these actors unless explicitly noted.
        std::vector<Rev::Actor3D*> actors;

        // Camera
        //--------------------------------------------------

        Graphics::UniformBuffer* cameraBuff = nullptr;

        glm::vec3 orbitPivot = { 0.0f, 0.0f, 0.0f };
        Core::Pos orbitMouse;
        bool hasOrbitPivot = false;

        glm::quat orientation = glm::normalize(
            glm::angleAxis(0.65f, glm::vec3(0.0f, 1.0f, 0.0f)) *
            glm::angleAxis(0.45f, glm::vec3(1.0f, 0.0f, 0.0f))
        );

        glm::quat pinOrientation = orientation;

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

            actors.clear();

            delete cameraBuff;
            cameraBuff = nullptr;
        }

        // Canvas and view management
        //--------------------------------------------------

        float canvasWidth() {

            float scale = shared->canvas->details.scale;

            if (scale <= 0.0f) {
                return std::max(float(shared->canvas->details.width), 1.0f);
            }

            return std::max(float(shared->canvas->details.width) / scale, 1.0f);
        }

        float canvasHeight() {

            float scale = shared->canvas->details.scale;

            if (scale <= 0.0f) {
                return std::max(float(shared->canvas->details.height), 1.0f);
            }

            return std::max(float(shared->canvas->details.height) / scale, 1.0f);
        }

        float canvasAspect() {

            float h = canvasHeight();

            if (h <= 0.0f) {
                return 1.0f;
            }

            return canvasWidth() / h;
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

            actors.erase(
                std::remove(
                    actors.begin(),
                    actors.end(),
                    actor
                ),
                actors.end()
            );

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

        glm::vec3 worldOnTargetPlane(
            Core::Pos mousePos,
            float scale
        ) {
            glm::vec3 rayOrigin;
            glm::vec3 rayDir;

            rayFromMouse(
                mousePos,
                rayOrigin,
                rayDir
            );

            glm::vec3 right;
            glm::vec3 up;
            glm::vec3 forward;

            cameraBasis(
                right,
                up,
                forward
            );

            glm::vec3 planePoint = target;
            glm::vec3 planeNormal = forward;

            float denom = glm::dot(
                rayDir,
                planeNormal
            );

            if (std::abs(denom) < 1e-6f) {
                return target;
            }

            float t = glm::dot(
                planePoint - rayOrigin,
                planeNormal
            ) / denom;

            return rayOrigin + rayDir * t;
        }

        glm::vec3 targetForScreenPoint(
            glm::vec3 worldPoint,
            Core::Pos mousePos,
            float scale
        ) {
            return targetForScreenPointWithOrientation(
                worldPoint,
                mousePos,
                scale,
                orientation
            );
        }

        void rayFromMouse(
            Core::Pos mousePos,
            glm::vec3& rayOrigin,
            glm::vec3& rayDir
        ) {
            float safeWidth = canvasWidth();
            float safeHeight = canvasHeight();

            float ndcX = (mousePos.x / safeWidth) * 2.0f - 1.0f;
            float ndcY = 1.0f - (mousePos.y / safeHeight) * 2.0f;

            glm::mat4 viewProj = viewProjMatrixFor(
                orientation,
                target
            );

            glm::mat4 invViewProj = glm::inverse(
                viewProj
            );

            glm::vec4 nearClip = {
                ndcX,
                ndcY,
                -1.0f,
                1.0f
            };

            glm::vec4 farClip = {
                ndcX,
                ndcY,
                1.0f,
                1.0f
            };

            glm::vec4 nearWorld = invViewProj * nearClip;
            glm::vec4 farWorld = invViewProj * farClip;

            nearWorld /= nearWorld.w;
            farWorld /= farWorld.w;

            rayOrigin = glm::vec3(nearWorld);

            rayDir = glm::normalize(
                glm::vec3(farWorld - nearWorld)
            );
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
            Hit& outHit
        ) {
            glm::vec3 rayOrigin;
            glm::vec3 rayDir;

            rayFromMouse(
                mousePos,
                rayOrigin,
                rayDir
            );

            outHit = Hit();

            float bestT = std::numeric_limits<float>::max();

            for (Rev::Actor3D* actor : actors) {

                if (!actor || !actor->visible) {
                    continue;
                }

                if (!actor->mesh) {
                    continue;
                }

                std::vector<Core::Vertex3>* pTriangles =
                    actor->mesh->getTriangles();

                if (!pTriangles) {
                    continue;
                }

                std::vector<Core::Vertex3>& triangles = *pTriangles;

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

                        outHit.hit = true;
                        outHit.actor = actor;
                        outHit.triangleId = i / 3;
                        outHit.point = rayOrigin + rayDir * t;
                        outHit.t = t;
                    }
                }
            }

            return outHit.hit;
        }

        bool hitTest(
            Core::Pos mousePos,
            glm::vec3& hitPoint
        ) {
            Hit hit;

            if (!hitTest(mousePos, hit)) {
                return false;
            }

            hitPoint = hit.point;

            return true;
        }

        glm::mat4 cameraRotation() {

            return glm::toMat4(
                orientation
            );
        }

        void cameraBasisFor(
            glm::quat q,
            glm::vec3& right,
            glm::vec3& up,
            glm::vec3& forward
        ) {
            right = glm::normalize(
                q * glm::vec3(1.0f, 0.0f, 0.0f)
            );

            up = glm::normalize(
                q * glm::vec3(0.0f, 1.0f, 0.0f)
            );

            forward = glm::normalize(
                q * glm::vec3(0.0f, 0.0f, -1.0f)
            );
        }

        void cameraBasis(
            glm::vec3& right,
            glm::vec3& up,
            glm::vec3& forward
        ) {
            cameraBasisFor(
                orientation,
                right,
                up,
                forward
            );
        }

        glm::mat4 viewMatrixFor(
            glm::quat q,
            glm::vec3 cameraTarget
        ) {
            glm::vec3 right;
            glm::vec3 up;
            glm::vec3 forward;

            cameraBasisFor(
                q,
                right,
                up,
                forward
            );

            glm::vec3 eye = cameraTarget - forward * distance;

            return glm::lookAt(
                eye,
                cameraTarget,
                up
            );
        }

        glm::mat4 projectionMatrix() {

            float aspect = canvasAspect();

            return glm::ortho(
                -orthoScale * aspect,
                orthoScale * aspect,
                -orthoScale,
                orthoScale,
                -100.0f,
                100.0f
            );
        }

        glm::mat4 viewProjMatrixFor(
            glm::quat q,
            glm::vec3 cameraTarget
        ) {
            return projectionMatrix() * viewMatrixFor(
                q,
                cameraTarget
            );
        }

        glm::vec3 targetForScreenPointWithOrientation(
            glm::vec3 worldPoint,
            Core::Pos mousePos,
            float scale,
            glm::quat q
        ) {
            glm::vec3 right;
            glm::vec3 up;
            glm::vec3 forward;

            cameraBasisFor(
                q,
                right,
                up,
                forward
            );

            float safeWidth = canvasWidth();
            float safeHeight = canvasHeight();
            float aspect = safeWidth / safeHeight;

            float ndcX = (mousePos.x / safeWidth) * 2.0f - 1.0f;
            float ndcY = 1.0f - (mousePos.y / safeHeight) * 2.0f;

            return (
                worldPoint
                - right * (ndcX * scale * aspect)
                - up * (ndcY * scale)
            );
        }

        void updateCamera() {

            glm::vec3 right;
            glm::vec3 up;
            glm::vec3 forward;

            cameraBasis(
                right,
                up,
                forward
            );

            glm::vec3 eye = target - forward * distance;

            CameraData data;

            data.viewProj = viewProjMatrixFor(
                orientation,
                target
            );

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

            orbitMouse = e.mouse.pos;
            hasOrbitPivot = false;

            Hit hit;

            if (hitTest(e.mouse.pos, hit)) {
                orbitPivot = hit.point;
                hasOrbitPivot = true;
            }

            else {
                orbitPivot = worldOnTargetPlane(
                    e.mouse.pos,
                    orthoScale
                );

                hasOrbitPivot = true;
            }

            target = targetForScreenPointWithOrientation(
                orbitPivot,
                orbitMouse,
                orthoScale,
                orientation
            );

            pinOrientation = orientation;
            pinTarget = target;

            Box::mouseDown(e);
        }

        void mouseDrag(Event& e) override {

            float rotateSensitivity = 0.008f;

            if (e.keyboard.shift) {

                glm::vec3 right;
                glm::vec3 up;
                glm::vec3 forward;

                cameraBasisFor(
                    pinOrientation,
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

                glm::vec3 pinRight;
                glm::vec3 pinUp;
                glm::vec3 pinForward;

                cameraBasisFor(
                    pinOrientation,
                    pinRight,
                    pinUp,
                    pinForward
                );

                float yawAngle =
                    -e.mouse.diff.x * rotateSensitivity;

                float pitchAngle =
                    -e.mouse.diff.y * rotateSensitivity;

                glm::quat yawRotation = glm::angleAxis(
                    yawAngle,
                    pinUp
                );

                glm::quat pitchRotation = glm::angleAxis(
                    pitchAngle,
                    pinRight
                );

                glm::quat nextOrientation = glm::normalize(
                    pitchRotation *
                    yawRotation *
                    pinOrientation
                );

                orientation = nextOrientation;

                if (hasOrbitPivot) {
                    target = targetForScreenPointWithOrientation(
                        orbitPivot,
                        orbitMouse,
                        orthoScale,
                        nextOrientation
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