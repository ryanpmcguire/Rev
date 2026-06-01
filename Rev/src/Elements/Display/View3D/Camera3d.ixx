module;

#include <algorithm>
#include <cmath>

#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

export module Rev.Element.View3d.Camera3d;

import Rev.Core.Pos;
import Rev.Core.Pos3;
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

        Core::Pos3 target = { 0.0f, 0.0f, 0.0f };
        Core::Pos3 pinTarget = { 0.0f, 0.0f, 0.0f };

        float distance = 4.0f;
        float orthoScale = 2.2f;

        // 0 = orthographic, 1 = wide perspective ("quake pro" FOV).
        float perspectiveBlend = 0.0f;

        static constexpr float kMaxPerspectiveFovY = 110.0f;
        static constexpr float kMinPerspectiveFovY = 25.0f;
        static constexpr float kPerspectiveBlendStep = 0.075f;

        static constexpr float kPerspectiveFlyFraction = 0.05f;
        static constexpr float kOrthoZoomInFactor = 0.94f;
        static constexpr float kOrthoZoomOutFactor = 1.0f / kOrthoZoomInFactor;
        static constexpr float kZoomSmoothRate = 24.0f;
        static constexpr float kOrthoSettleEpsilon = 0.0001f;
        static constexpr float kTargetSettleFraction = 0.001f;

        float orthoScaleGoal = 2.2f;
        Core::Pos3 targetGoal = { 0.0f, 0.0f, 0.0f };
        bool zoomGoalsInitialized = false;
        float zoomSceneScale = 1.0f;

        Core::Pos3 orbitPivot = { 0.0f, 0.0f, 0.0f };
        Core::Pos3 orbitEyeOffset = { 0.0f, 0.0f, 0.0f };
        Core::Pos orbitMouse;
        Core::Pos orbitLastMouse;
        bool hasOrbitPivot = false;

        float rotateSensitivity = glm::two_pi<float>() / 800.0f;

        // GLM boundary helpers
        //--------------------------------------------------
        //
        // Camera3d still uses GLM for matrix/quaternion operations.
        // Pos/Pos3 are used for vector-space state and arithmetic.

        static glm::vec3 toGlm(const Core::Pos3& p) {
            return glm::vec3(p.x, p.y, p.z);
        }

        static Core::Pos3 fromGlm(const glm::vec3& p) {
            return Core::Pos3(p.x, p.y, p.z);
        }

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

        // Mouse wheels often report large deltas (e.g. ±120); trackpads report smaller values.
        static float normalizeWheelStep(float wheelY) {
            const float absWheel = std::abs(wheelY);

            if (absWheel < 1e-6f) {
                return 0.0f;
            }

            const float step = absWheel >= 1.0f ? 1.0f : absWheel;

            return std::copysign(step, wheelY);
        }

        // Initialization
        //--------------------------------------------------

        glm::quat orientationFromForwardUp(
            Core::Pos3 forward,
            Core::Pos3 upHint = { 0.0f, 0.0f, 1.0f }
        ) const {
            forward = forward.normalized();

            Core::Pos3 right = forward.cross(upHint);

            if (right.pythag() < 1e-6f) {
                right = forward.cross({ 0.0f, 1.0f, 0.0f });
            }

            right = right.normalized();

            Core::Pos3 up = right.cross(forward).normalized();

            glm::mat3 basis(
                toGlm(right),
                toGlm(up),
                toGlm(forward * -1.0f)
            );

            return glm::normalize(
                glm::quat_cast(basis)
            );
        }

        void setDefaultView() {

            Core::Pos3 forward = Core::Pos3(
                -1.0f,
                -1.0f,
                -1.41421356f
            ).normalized();

            orientation = orientationFromForwardUp(
                forward,
                { 0.0f, 0.0f, 1.0f }
            );

            pin();
        }

        void fitBounds(
            Core::Pos3 min,
            Core::Pos3 max,
            float width,
            float height,
            float padding = 1.15f
        ) {
            Core::Pos3 center = (min + max) * 0.5f;

            Core::Pos3 corners[8] = {
                { min.x, min.y, min.z },
                { max.x, min.y, min.z },
                { min.x, max.y, min.z },
                { max.x, max.y, min.z },
                { min.x, min.y, max.z },
                { max.x, min.y, max.z },
                { min.x, max.y, max.z },
                { max.x, max.y, max.z }
            };

            Core::Pos3 right;
            Core::Pos3 up;
            Core::Pos3 forward;

            basis(right, up, forward);

            float minX = 0.0f;
            float maxX = 0.0f;
            float minY = 0.0f;
            float maxY = 0.0f;
            float minZ = 0.0f;
            float maxZ = 0.0f;

            bool first = true;

            for (Core::Pos3& corner : corners) {

                Core::Pos3 d = corner - center;

                float x = d.dot(right);
                float y = d.dot(up);
                float z = d.dot(forward);

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

            float radius = (max - min).pythag() * 0.5f;

            distance = std::max(4.0f, radius * 2.0f);

            syncZoomGoalsFromCurrent();
            pin();
        }

        // Basis
        //--------------------------------------------------

        void basisFor(
            glm::quat q,
            Core::Pos3& right,
            Core::Pos3& up,
            Core::Pos3& forward
        ) const {
            right = fromGlm(glm::normalize(q * glm::vec3(1.0f, 0.0f, 0.0f)));
            up = fromGlm(glm::normalize(q * glm::vec3(0.0f, 1.0f, 0.0f)));
            forward = fromGlm(glm::normalize(q * glm::vec3(0.0f, 0.0f, -1.0f)));
        }

        void basis(
            Core::Pos3& right,
            Core::Pos3& up,
            Core::Pos3& forward
        ) const {
            basisFor(orientation, right, up, forward);
        }

        Core::Pos3 eye() const {

            Core::Pos3 right;
            Core::Pos3 up;
            Core::Pos3 forward;

            basis(right, up, forward);

            return target - forward * distance;
        }

        // Projection
        //--------------------------------------------------

        bool usesOrthographicProjection() const {
            return perspectiveBlend <= 0.0001f;
        }

        float perspectiveFovDegrees() const {

            float t = std::clamp(perspectiveBlend, 0.0f, 1.0f);

            return kMinPerspectiveFovY + (kMaxPerspectiveFovY - kMinPerspectiveFovY) * t;
        }

        float perspectiveNearClip() const {
            return 0.05f;
        }

        float perspectiveFarClip() const {
            return std::max(2000.0f, distance * 25.0f);
        }

        // Visible half-height in world units at the view center.
        float verticalHalfExtent() const {

            float perspHalf = distance * std::tan(
                glm::radians(perspectiveFovDegrees() * 0.5f)
            );

            float t = std::clamp(perspectiveBlend, 0.0f, 1.0f);

            return glm::mix(orthoScale, perspHalf, t);
        }

        glm::mat4 orthographicProjection(float aspectRatio) const {
            return glm::ortho(
                -orthoScale * aspectRatio, orthoScale * aspectRatio,
                -orthoScale, orthoScale,
                nearClip, farClip
            );
        }

        glm::mat4 perspectiveProjection(float aspectRatio) const {
            return glm::perspective(
                glm::radians(perspectiveFovDegrees()),
                aspectRatio,
                perspectiveNearClip(),
                perspectiveFarClip()
            );
        }

        static glm::mat4 mixProjection(
            const glm::mat4& ortho,
            const glm::mat4& persp,
            float t
        ) {
            glm::mat4 out;

            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r) {
                    out[c][r] = glm::mix(ortho[c][r], persp[c][r], t);
                }
            }

            return out;
        }

        // Matrices
        //--------------------------------------------------

        glm::mat4 viewMatrixFor(
            glm::quat q,
            Core::Pos3 cameraTarget
        ) const {
            Core::Pos3 right;
            Core::Pos3 up;
            Core::Pos3 forward;

            basisFor(q, right, up, forward);

            return glm::lookAt(
                toGlm(cameraTarget - forward * distance),
                toGlm(cameraTarget),
                toGlm(up)
            );
        }

        glm::mat4 projectionMatrix(
            float width,
            float height
        ) const {
            float a = aspect(width, height);

            glm::mat4 orthoMat = orthographicProjection(a);

            if (usesOrthographicProjection()) {
                return orthoMat;
            }

            float t = std::clamp(perspectiveBlend, 0.0f, 1.0f);

            if (t >= 0.9999f) {
                return perspectiveProjection(a);
            }

            return mixProjection(orthoMat, perspectiveProjection(a), t);
        }

        glm::mat4 viewProjMatrixFor(
            glm::quat q,
            Core::Pos3 cameraTarget,
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

        Core::Pos ndcFromMouse(
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
            return rayFromMouseFor(orientation, target, mousePos, width, height);
        }

        Ray rayFromMouseFor(
            glm::quat q,
            Core::Pos3 cameraTarget,
            Core::Pos mousePos,
            float width,
            float height
        ) const {
            Core::Pos ndc = ndcFromMouse(mousePos, width, height);

            glm::mat4 invViewProj = glm::inverse(
                viewProjMatrixFor(q, cameraTarget, width, height)
            );

            glm::vec4 nearWorld = invViewProj * glm::vec4(ndc.x, ndc.y, -1.0f, 1.0f);
            glm::vec4 farWorld = invViewProj * glm::vec4(ndc.x, ndc.y, 1.0f, 1.0f);

            nearWorld /= nearWorld.w;
            farWorld /= farWorld.w;

            Ray ray;

            ray.origin = fromGlm(glm::vec3(nearWorld));
            ray.direction = fromGlm(glm::normalize(glm::vec3(farWorld - nearWorld)));

            return ray;
        }

        Core::Pos projectWorldToNdc(
            Core::Pos3 world,
            glm::quat q,
            Core::Pos3 cameraTarget,
            float width,
            float height
        ) const {
            glm::mat4 viewProj = viewProjMatrixFor(q, cameraTarget, width, height);
            glm::vec4 clip = viewProj * glm::vec4(toGlm(world), 1.0f);

            if (std::abs(clip.w) < 1e-8f) {
                return { 0.0f, 0.0f };
            }

            clip /= clip.w;

            return { clip.x, clip.y };
        }

        Core::Pos3 targetForScreenPointOrthographic(
            Core::Pos3 worldPoint,
            Core::Pos mousePos,
            float width,
            float height,
            glm::quat q
        ) const {
            Core::Pos3 right;
            Core::Pos3 up;
            Core::Pos3 forward;

            basisFor(q, right, up, forward);

            float safeW = safeWidth(width);
            float safeH = safeHeight(height);
            float a = aspect(safeW, safeH);

            Core::Pos ndc = ndcFromMouse(mousePos, safeW, safeH);

            float halfY = orthoScale;
            float halfX = halfY * a;

            return (
                worldPoint
                - right * (ndc.x * halfX)
                - up * (ndc.y * halfY)
            );
        }

        Core::Pos3 targetForScreenPointPerspective(
            Core::Pos3 worldPoint,
            Core::Pos mousePos,
            float width,
            float height,
            glm::quat q,
            Core::Pos3 initialGuess
        ) const {
            Core::Pos3 right;
            Core::Pos3 up;
            Core::Pos3 forward;

            basisFor(q, right, up, forward);

            float safeW = safeWidth(width);
            float safeH = safeHeight(height);

            Core::Pos desiredNdc = ndcFromMouse(mousePos, safeW, safeH);

            Core::Pos3 guess = initialGuess;

            Core::Pos ndc = projectWorldToNdc(worldPoint, q, guess, width, height);
            Core::Pos err = {
                desiredNdc.x - ndc.x,
                desiredNdc.y - ndc.y
            };

            if (err.x * err.x + err.y * err.y < 1e-8f) {
                return guess;
            }

            constexpr int kMaxIter = 12;
            constexpr float kEps = 0.001f;

            for (int iter = 0; iter < kMaxIter; ++iter) {

                ndc = projectWorldToNdc(worldPoint, q, guess, width, height);
                err = {
                    desiredNdc.x - ndc.x,
                    desiredNdc.y - ndc.y
                };

                if (err.x * err.x + err.y * err.y < 1e-8f) {
                    break;
                }

                Core::Pos ndcRight = projectWorldToNdc(
                    worldPoint, q, guess + right * kEps, width, height
                );

                Core::Pos ndcUp = projectWorldToNdc(
                    worldPoint, q, guess + up * kEps, width, height
                );

                float dNdx_dRx = (ndcRight.x - ndc.x) / kEps;
                float dNdy_dRy = (ndcRight.y - ndc.y) / kEps;
                float dNdx_dUx = (ndcUp.x - ndc.x) / kEps;
                float dNdy_dUy = (ndcUp.y - ndc.y) / kEps;

                float det = dNdx_dRx * dNdy_dUy - dNdx_dUx * dNdy_dRy;

                if (std::abs(det) < 1e-8f) {
                    break;
                }

                float dr = (err.x * dNdy_dUy - err.y * dNdx_dUx) / det;
                float du = (-err.x * dNdy_dRy + err.y * dNdx_dRx) / det;

                guess = guess + right * dr + up * du;
            }

            return guess;
        }

        Core::Pos3 worldOnTargetPlane(
            Core::Pos mousePos,
            float width,
            float height
        ) const {
            Ray ray = rayFromMouse(mousePos, width, height);

            Core::Pos3 right;
            Core::Pos3 up;
            Core::Pos3 forward;

            basis(right, up, forward);

            float denom = ray.direction.dot(forward);

            if (std::abs(denom) < 1e-6f) { return target; }

            float t = (target -  ray.origin).dot(forward) / denom;

            return  ray.origin + ray.direction * t;
        }

        Core::Pos3 targetForScreenPointWithOrientation(
            Core::Pos3 worldPoint,
            Core::Pos mousePos,
            float width,
            float height,
            glm::quat q
        ) const {
            if (usesOrthographicProjection()) {
                return targetForScreenPointOrthographic(
                    worldPoint,
                    mousePos,
                    width,
                    height,
                    q
                );
            }

            return targetForScreenPointPerspective(
                worldPoint,
                mousePos,
                width,
                height,
                q,
                target
            );
        }

        Core::Pos3 targetForScreenPoint(
            Core::Pos3 worldPoint,
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

        void ensureZoomGoalsInitialized() {

            if (!zoomGoalsInitialized) {
                syncZoomGoalsFromCurrent();
            }
        }

        void syncZoomGoalsFromCurrent() {

            orthoScaleGoal = orthoScale;
            targetGoal = target;
            zoomGoalsInitialized = true;
        }

        void cancelZoomAnimation() {
            syncZoomGoalsFromCurrent();
        }

        void applyWheelZoom(
            Event& e,
            float width,
            float height,
            float sceneAverageDimension
        ) {
            if (e.keyboard.alt) {

                float direction = (e.mouse.wheel.y > 0.0f ? 1.0f : -1.0f);

                perspectiveBlend += direction * kPerspectiveBlendStep;
                perspectiveBlend = std::clamp(perspectiveBlend, 0.0f, 1.0f);

                return;
            }

            if (std::abs(e.mouse.wheel.y) < 1e-6f) {
                return;
            }

            ensureZoomGoalsInitialized();

            zoomSceneScale = std::max(sceneAverageDimension, 0.01f);

            if (usesOrthographicProjection()) {

                Core::Pos3 savedTarget = target;
                float savedScale = orthoScale;

                target = targetGoal;
                orthoScale = orthoScaleGoal;

                Core::Pos3 before = worldOnTargetPlane(e.mouse.pos, width, height);

                float zoom = (
                    e.mouse.wheel.y > 0.0f
                        ? kOrthoZoomInFactor
                        : kOrthoZoomOutFactor
                );

                orthoScaleGoal *= zoom;
                orthoScaleGoal = std::clamp(orthoScaleGoal, 0.05f, 100.0f);

                orthoScale = orthoScaleGoal;

                Core::Pos3 after = worldOnTargetPlane(e.mouse.pos, width, height);

                targetGoal += before - after;

                target = savedTarget;
                orthoScale = savedScale;
            }

            else {

                Ray ray = rayFromMouse(e.mouse.pos, width, height);

                const float wheelStep = normalizeWheelStep(e.mouse.wheel.y);

                const float flyStep =
                    wheelStep *
                    zoomSceneScale *
                    kPerspectiveFlyFraction;

                targetGoal += ray.direction * flyStep;
            }
        }

        // Smooth toward zoom/fly goals. Returns false when settled.
        bool stepZoomAnimation(float deltaMs) {

            if (!zoomGoalsInitialized) {
                return false;
            }

            if (deltaMs <= 0.0f) {
                deltaMs = 10.0f;
            }

            const float dt = deltaMs / 1000.0f;
            const float alpha = 1.0f - std::exp(-kZoomSmoothRate * dt);

            const float scaleError = orthoScaleGoal - orthoScale;
            orthoScale += scaleError * alpha;

            const Core::Pos3 targetError = targetGoal - target;
            target += targetError * alpha;

            const float scaleSettleEpsilon =
                std::max(kOrthoSettleEpsilon, zoomSceneScale * kTargetSettleFraction);

            const float targetSettleEpsilon =
                std::max(kOrthoSettleEpsilon, zoomSceneScale * kTargetSettleFraction);

            const bool scaleSettled =
                std::abs(orthoScaleGoal - orthoScale) < scaleSettleEpsilon;

            const bool targetSettled =
                targetError.pythag() < targetSettleEpsilon;

            if (scaleSettled && targetSettled) {
                orthoScale = orthoScaleGoal;
                target = targetGoal;
                return false;
            }

            return true;
        }

        void pin() {
            pinOrientation = orientation;
            pinTarget = target;
        }

        void mouseDown(
            Event& e,
            Core::Pos3 hitPoint,
            float width,
            float height
        ) {
            cancelZoomAnimation();

            orbitPivot = hitPoint;
            orbitMouse = e.mouse.pos;
            orbitLastMouse = e.mouse.pos;
            hasOrbitPivot = true;

            if (usesOrthographicProjection()) {
                target = targetForScreenPoint(
                    orbitPivot,
                    orbitMouse,
                    width,
                    height
                );
            }

            else {
                orbitEyeOffset = eye() - orbitPivot;
            }

            pin();
        }

        void panFromPinned(
            Event& e,
            float width,
            float height
        ) {
            Core::Pos3 right;
            Core::Pos3 up;
            Core::Pos3 forward;

            basisFor(pinOrientation, right, up, forward);

            float worldPerPixel = (2.0f * verticalHalfExtent()) / safeHeight(height);
            Core::Pos3 pan = (
                right * -e.mouse.diff.x +
                up * e.mouse.diff.y
            ) * worldPerPixel;

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

            Core::Pos3 right;
            Core::Pos3 up;
            Core::Pos3 forward;

            basis(right, up, forward);

            glm::quat yaw = glm::angleAxis(
                -step.x * rotateSensitivity,
                toGlm(up)
            );

            glm::quat pitch = glm::angleAxis(
                -step.y * rotateSensitivity,
                toGlm(right)
            );

            glm::quat delta = glm::normalize(pitch * yaw);

            if (usesOrthographicProjection()) {

                orientation = glm::normalize(delta * orientation);

                target = targetForScreenPointWithOrientation(
                    orbitPivot,
                    orbitMouse,
                    width,
                    height,
                    orientation
                );
            }

            else {

                orbitEyeOffset = fromGlm(delta * toGlm(orbitEyeOffset));

                const float radius = orbitEyeOffset.pythag();

                if (radius < 1e-6f) {
                    orbitLastMouse = e.mouse.pos;
                    return;
                }

                const Core::Pos3 lookForward = (-1.0f *orbitEyeOffset).normalized();

                orientation = orientationFromForwardUp(lookForward, up);
                target = orbitPivot;
                distance = radius;

                syncZoomGoalsFromCurrent();
            }

            orbitLastMouse = e.mouse.pos;
        }

        void mouseDrag(
            Event& e,
            float width,
            float height
        ) {
            cancelZoomAnimation();

            if (e.keyboard.shift) { panFromPinned(e, width, height); }
            else { orbitIncremental(e, width, height); }
        }

        void mouseWheel(
            Event& e,
            float width,
            float height,
            float sceneAverageDimension
        ) {
            applyWheelZoom(e, width, height, sceneAverageDimension);
        }
    };
}
