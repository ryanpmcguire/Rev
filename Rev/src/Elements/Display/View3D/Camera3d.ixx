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
import Rev.Element.View3d.Actor3d;

export namespace Rev::Element::View3d {

    struct Camera {

        glm::quat orientation = glm::normalize(
            glm::angleAxis(0.65f, glm::vec3(0.0f, 1.0f, 0.0f)) *
            glm::angleAxis(0.45f, glm::vec3(1.0f, 0.0f, 0.0f))
        );

        glm::quat pinOrientation = orientation;

        glm::vec3 target = { 0.0f, 0.0f, 0.0f };
        glm::vec3 pinTarget = { 0.0f, 0.0f, 0.0f };

        float distance = 4.0f;
        float orthoScale = 2.2f;

        // Canvas
        //--------------------------------------------------

        static float safeWidth(float width) {

            return std::max(width, 1.0f);
        }

        static float safeHeight(float height) {

            return std::max(height, 1.0f);
        }

        static float aspect(
            float width,
            float height
        ) {
            return safeWidth(width) / safeHeight(height);
        }

        // Basis
        //--------------------------------------------------

        void basisFor(
            glm::quat q,
            glm::vec3& right,
            glm::vec3& up,
            glm::vec3& forward
        ) const {
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

        void basis(
            glm::vec3& right,
            glm::vec3& up,
            glm::vec3& forward
        ) const {
            basisFor(
                orientation,
                right,
                up,
                forward
            );
        }

        glm::vec3 eye() const {

            glm::vec3 right;
            glm::vec3 up;
            glm::vec3 forward;

            basis(
                right,
                up,
                forward
            );

            return target - forward * distance;
        }

        // Matrices
        //--------------------------------------------------

        glm::mat4 viewMatrixFor(
            glm::quat q,
            glm::vec3 cameraTarget
        ) const {
            glm::vec3 right;
            glm::vec3 up;
            glm::vec3 forward;

            basisFor(
                q,
                right,
                up,
                forward
            );

            glm::vec3 eyePos = cameraTarget - forward * distance;

            return glm::lookAt(
                eyePos,
                cameraTarget,
                up
            );
        }

        glm::mat4 projectionMatrix(
            float width,
            float height
        ) const {
            float a = aspect(
                width,
                height
            );

            return glm::ortho(
                -orthoScale * a,
                 orthoScale * a,
                -orthoScale,
                 orthoScale,
                -100.0f,
                 100.0f
            );
        }

        glm::mat4 viewProjMatrixFor(
            glm::quat q,
            glm::vec3 cameraTarget,
            float width,
            float height
        ) const {
            return projectionMatrix(
                width,
                height
            ) * viewMatrixFor(
                q,
                cameraTarget
            );
        }

        glm::mat4 viewProjMatrix(
            float width,
            float height
        ) const {
            return viewProjMatrixFor(
                orientation,
                target,
                width,
                height
            );
        }

        // Mouse/world mapping
        //--------------------------------------------------

        Ray rayFromMouse(
            Core::Pos mousePos,
            float width,
            float height
        ) const {
            float safeW = safeWidth(width);
            float safeH = safeHeight(height);

            float ndcX = (mousePos.x / safeW) * 2.0f - 1.0f;
            float ndcY = 1.0f - (mousePos.y / safeH) * 2.0f;

            glm::mat4 invViewProj = glm::inverse(
                viewProjMatrix(
                    safeW,
                    safeH
                )
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

            Ray ray;

            ray.origin = glm::vec3(
                nearWorld
            );

            ray.direction = glm::normalize(
                glm::vec3(farWorld - nearWorld)
            );

            return ray;
        }

        glm::vec3 worldOnTargetPlane(
            Core::Pos mousePos,
            float width,
            float height
        ) const {
            Ray ray = rayFromMouse(
                mousePos,
                width,
                height
            );

            glm::vec3 right;
            glm::vec3 up;
            glm::vec3 forward;

            basis(
                right,
                up,
                forward
            );

            glm::vec3 planePoint = target;
            glm::vec3 planeNormal = forward;

            float denom = glm::dot(
                ray.direction,
                planeNormal
            );

            if (std::abs(denom) < 1e-6f) {
                return target;
            }

            float t = glm::dot(
                planePoint - ray.origin,
                planeNormal
            ) / denom;

            return ray.origin + ray.direction * t;
        }

        glm::vec3 targetForScreenPointWithOrientation(
            glm::vec3 worldPoint,
            Core::Pos mousePos,
            float width,
            float height,
            glm::quat q
        ) const {
            glm::vec3 right;
            glm::vec3 up;
            glm::vec3 forward;

            basisFor(
                q,
                right,
                up,
                forward
            );

            float safeW = safeWidth(width);
            float safeH = safeHeight(height);
            float a = aspect(safeW, safeH);

            float ndcX = (mousePos.x / safeW) * 2.0f - 1.0f;
            float ndcY = 1.0f - (mousePos.y / safeH) * 2.0f;

            return (
                worldPoint
                - right * (ndcX * orthoScale * a)
                - up * (ndcY * orthoScale)
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

        void panFromPinned(
            Core::Pos mouseDiff,
            float width,
            float height
        ) {
            glm::vec3 right;
            glm::vec3 up;
            glm::vec3 forward;

            basisFor(
                pinOrientation,
                right,
                up,
                forward
            );

            float worldPerPixel = (
                2.0f * orthoScale
            ) / safeHeight(height);

            glm::vec3 pan =
                (-right * mouseDiff.x + up * mouseDiff.y)
                * worldPerPixel;

            target = pinTarget + pan;
        }

        void orbitFromPinned(
            Core::Pos mouseDiff,
            glm::vec3 orbitPivot,
            Core::Pos orbitMouse,
            float width,
            float height,
            float rotateSensitivity = 0.008f
        ) {
            glm::vec3 pinRight;
            glm::vec3 pinUp;
            glm::vec3 pinForward;

            basisFor(
                pinOrientation,
                pinRight,
                pinUp,
                pinForward
            );

            float yawAngle =
                -mouseDiff.x * rotateSensitivity;

            float pitchAngle =
                -mouseDiff.y * rotateSensitivity;

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

            target = targetForScreenPointWithOrientation(
                orbitPivot,
                orbitMouse,
                width,
                height,
                nextOrientation
            );
        }

        void zoomAtMouse(
            Core::Pos mousePos,
            float wheelY,
            float width,
            float height
        ) {
            glm::vec3 before = worldOnTargetPlane(
                mousePos,
                width,
                height
            );

            float zoom = (
                wheelY > 0.0f
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
                mousePos,
                width,
                height
            );

            target += before - after;
        }
    };
}