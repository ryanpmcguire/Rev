module;

#include <algorithm>
#include <cmath>

#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

export module Rev.Element.View3d.Camera3d;

import Rev.Core.Pos;
import Rev.Element.Event;
import Rev.Element.View3d.Actor3d;

export namespace Rev::Element::View3d {

    struct Camera {

        glm::quat orientation = glm::normalize(
            glm::angleAxis(0.65f, glm::vec3(0.0f, 1.0f, 0.0f)) *
            glm::angleAxis(0.45f, glm::vec3(1.0f, 0.0f, 0.0f))
        );

        float nearClip = -1000.0f;
        float farClip = 1000.0f;

        glm::quat pinOrientation = orientation;

        glm::vec3 target = { 0.0f, 0.0f, 0.0f };
        glm::vec3 pinTarget = { 0.0f, 0.0f, 0.0f };

        float distance = 4.0f;
        float orthoScale = 2.2f;

        glm::vec3 orbitPivot = { 0.0f, 0.0f, 0.0f };
        Core::Pos orbitMouse;
        Core::Pos orbitLastMouse;
        bool hasOrbitPivot = false;

        float rotateSensitivity = glm::two_pi<float>() / 800.0f;

        // Canvas
        //--------------------------------------------------

        static float safeWidth(float width) {
            return std::max(width, 1.0f);
        }

        static float safeHeight(float height) {
            return std::max(height, 1.0f);
        }

        static float aspect(float width, float height) {
            return safeWidth(width) / safeHeight(height);
        }

        // Initialization
        //--------------------------------------------------

        glm::quat orientationFromForwardUp(
            glm::vec3 forward,
            glm::vec3 upHint = { 0.0f, 0.0f, 1.0f }
        ) const {
            forward = glm::normalize(forward);

            glm::vec3 right = glm::cross(forward, upHint);

            if (glm::length(right) < 1e-6f) {
                right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
            }

            right = glm::normalize(right);

            glm::vec3 up = glm::normalize(
                glm::cross(right, forward)
            );

            glm::mat3 basis(
                right,
                up,
                -forward
            );

            return glm::normalize(
                glm::quat_cast(basis)
            );
        }

        void setDefaultView() {

            glm::vec3 forward = glm::normalize(
                glm::vec3(
                    -1.0f,
                    -1.0f,
                    -1.41421356f
                )
            );

            orientation = orientationFromForwardUp(
                forward,
                { 0.0f, 0.0f, 1.0f }
            );

            pin();
        }

        void fitBounds(
            glm::vec3 min,
            glm::vec3 max,
            float width,
            float height,
            float padding = 1.15f
        ) {
            glm::vec3 center = (min + max) * 0.5f;

            glm::vec3 corners[8] = {
                { min.x, min.y, min.z },
                { max.x, min.y, min.z },
                { min.x, max.y, min.z },
                { max.x, max.y, min.z },
                { min.x, min.y, max.z },
                { max.x, min.y, max.z },
                { min.x, max.y, max.z },
                { max.x, max.y, max.z }
            };

            glm::vec3 right, up, forward;
            basis(right, up, forward);

            float minX = 0.0f;
            float maxX = 0.0f;
            float minY = 0.0f;
            float maxY = 0.0f;
            float minZ = 0.0f;
            float maxZ = 0.0f;

            bool first = true;

            for (glm::vec3& corner : corners) {

                glm::vec3 d = corner - center;

                float x = glm::dot(d, right);
                float y = glm::dot(d, up);
                float z = glm::dot(d, forward);

                if (first) {
                    minX = maxX = x;
                    minY = maxY = y;
                    minZ = maxZ = z;
                    first = false;
                    continue;
                }

                minX = std::min(minX, x);
                maxX = std::max(maxX, x);

                minY = std::min(minY, y);
                maxY = std::max(maxY, y);

                minZ = std::min(minZ, z);
                maxZ = std::max(maxZ, z);
            }

            float halfX = (maxX - minX) * 0.5f;
            float halfY = (maxY - minY) * 0.5f;

            float a = aspect(width, height);

            target = center;
            orthoScale = std::max(halfY, halfX / a) * padding;

            if (orthoScale < 0.01f) {
                orthoScale = 1.0f;
            }

            float radius = glm::length(max - min) * 0.5f;

            distance = std::max(4.0f, radius * 2.0f);

            pin();
        }

        // Basis
        //--------------------------------------------------

        void basisFor(
            glm::quat q,
            glm::vec3& right,
            glm::vec3& up,
            glm::vec3& forward
        ) const {
            right = glm::normalize(q * glm::vec3(1.0f, 0.0f, 0.0f));
            up = glm::normalize(q * glm::vec3(0.0f, 1.0f, 0.0f));
            forward = glm::normalize(q * glm::vec3(0.0f, 0.0f, -1.0f));
        }

        void basis(
            glm::vec3& right,
            glm::vec3& up,
            glm::vec3& forward
        ) const {
            basisFor(orientation, right, up, forward);
        }

        glm::vec3 eye() const {

            glm::vec3 right, up, forward;
            basis(right, up, forward);

            return target - forward * distance;
        }

        // Matrices
        //--------------------------------------------------

        glm::mat4 viewMatrixFor(
            glm::quat q,
            glm::vec3 cameraTarget
        ) const {
            glm::vec3 right, up, forward;
            basisFor(q, right, up, forward);

            return glm::lookAt(
                cameraTarget - forward * distance,
                cameraTarget,
                up
            );
        }

        glm::mat4 projectionMatrix(
            float width,
            float height
        ) const {
            float a = aspect(width, height);

            return glm::ortho(
                -orthoScale * a, orthoScale * a,
                -orthoScale, orthoScale,
                nearClip, farClip
            );
        }

        glm::mat4 viewProjMatrixFor(
            glm::quat q,
            glm::vec3 cameraTarget,
            float width,
            float height
        ) const {
            return projectionMatrix(width, height) * viewMatrixFor(q, cameraTarget);
        }

        glm::mat4 viewProjMatrix(
            float width,
            float height
        ) const {
            return viewProjMatrixFor(orientation, target, width, height);
        }

        // Mouse/world mapping
        //--------------------------------------------------

        glm::vec2 ndcFromMouse(
            Core::Pos mousePos,
            float width,
            float height
        ) const {
            float safeW = safeWidth(width);
            float safeH = safeHeight(height);

            return {
                (mousePos.x / safeW) * 2.0f - 1.0f,
                1.0f - (mousePos.y / safeH) * 2.0f
            };
        }

        Ray rayFromMouse(
            Core::Pos mousePos,
            float width,
            float height
        ) const {
            glm::vec2 ndc = ndcFromMouse(mousePos, width, height);

            glm::mat4 invViewProj = glm::inverse(viewProjMatrix(width, height));

            glm::vec4 nearWorld = invViewProj * glm::vec4(ndc.x, ndc.y, -1.0f, 1.0f);
            glm::vec4 farWorld = invViewProj * glm::vec4(ndc.x, ndc.y, 1.0f, 1.0f);

            nearWorld /= nearWorld.w;
            farWorld /= farWorld.w;

            Ray ray;
            ray.origin = glm::vec3(nearWorld);
            ray.direction = glm::normalize(glm::vec3(farWorld - nearWorld));

            return ray;
        }

        glm::vec3 worldOnTargetPlane(
            Core::Pos mousePos,
            float width,
            float height
        ) const {
            Ray ray = rayFromMouse(mousePos, width, height);

            glm::vec3 right, up, forward;
            basis(right, up, forward);

            float denom = glm::dot(ray.direction, forward);

            if (std::abs(denom) < 1e-6f) { return target; }

            float t = glm::dot(target - ray.origin, forward) / denom;

            return ray.origin + ray.direction * t;
        }

        glm::vec3 targetForScreenPointWithOrientation(
            glm::vec3 worldPoint,
            Core::Pos mousePos,
            float width,
            float height,
            glm::quat q
        ) const {
            glm::vec3 right, up, forward;
            basisFor(q, right, up, forward);

            float safeW = safeWidth(width);
            float safeH = safeHeight(height);
            float a = aspect(safeW, safeH);

            glm::vec2 ndc = ndcFromMouse(mousePos, safeW, safeH);

            return (
                worldPoint
                - right * (ndc.x * orthoScale * a)
                - up * (ndc.y * orthoScale)
            );
        }

        glm::vec3 targetForScreenPoint(
            glm::vec3 worldPoint,
            Core::Pos mousePos,
            float width,
            float height
        ) const {
            return targetForScreenPointWithOrientation(
                worldPoint,
                mousePos,
                width,
                height,
                orientation
            );
        }

        // Interaction
        //--------------------------------------------------

        void pin() {
            pinOrientation = orientation;
            pinTarget = target;
        }

        void mouseDown(
            Event& e,
            glm::vec3 hitPoint,
            float width,
            float height
        ) {
            orbitPivot = hitPoint;
            orbitMouse = e.mouse.pos;
            orbitLastMouse = e.mouse.pos;
            hasOrbitPivot = true;

            target = targetForScreenPoint(
                orbitPivot,
                orbitMouse,
                width,
                height
            );

            pin();
        }

        void panFromPinned(
            Event& e,
            float width,
            float height
        ) {
            glm::vec3 right, up, forward;
            basisFor(pinOrientation, right, up, forward);

            float worldPerPixel = (2.0f * orthoScale) / safeHeight(height);
            glm::vec3 pan = (-right * e.mouse.diff.x + up * e.mouse.diff.y) * worldPerPixel;

            target = pinTarget + pan;
        }

        void orbitIncremental(
            Event& e,
            float width,
            float height
        ) {
            if (!hasOrbitPivot) { return; }

            Core::Pos step = e.mouse.pos - orbitLastMouse;

            if (std::abs(step.x) < 0.0001f && std::abs(step.y) < 0.0001f) {
                return;
            }

            glm::vec3 right, up, forward;
            basis(right, up, forward);

            glm::quat yaw = glm::angleAxis(
                -step.x * rotateSensitivity,
                up
            );

            glm::quat pitch = glm::angleAxis(
                -step.y * rotateSensitivity,
                right
            );

            orientation = glm::normalize(pitch * yaw * orientation);

            target = targetForScreenPointWithOrientation(
                orbitPivot,
                orbitMouse,
                width,
                height,
                orientation
            );

            orbitLastMouse = e.mouse.pos;
        }

        void mouseDrag(
            Event& e,
            float width,
            float height
        ) {
            if (e.keyboard.shift) { panFromPinned(e, width, height); }
            else { orbitIncremental(e, width, height); }
        }

        void mouseWheel(
            Event& e,
            float width,
            float height
        ) {
            glm::vec3 before = worldOnTargetPlane(e.mouse.pos, width, height);

            float zoom = (e.mouse.wheel.y > 0.0f ? 0.9f : 1.1f);

            orthoScale *= zoom;
            orthoScale = std::clamp(orthoScale, 0.05f, 100.0f);

            glm::vec3 after = worldOnTargetPlane(e.mouse.pos, width, height);

            target += before - after;
        }
    };
}