module;

#include <vector>
#include <cmath>

export module Cam.Gui.ToolPath;

import Rev.Graphics.Canvas;

import Rev.Core.Pos3;
import Rev.Core.Color;
import Rev.Core.Vertex3;

import Rev.Element.View3d;
import Rev.Element.View3d.Actor3d;

import Rev.Primitive.Lines3d;
import Rev.Primitive.Mesh3d;

import Cam.App.MaterialState;
import Cam.App.ToolPath;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;
    using namespace Rev::Core;

    struct ToolPath {

        View3d::Actor* actor = nullptr;
        View3d::Actor* toolPreviewActor = nullptr;

        std::vector<Vertex3> lines;
        std::vector<Vertex3> toolPreviewTriangles;

        // Create / destroy
        //--------------------------------------------------

        void create(Rev::Graphics::Canvas* canvas) {

            actor = new View3d::Actor();

            actor->visible = false;
            actor->selectable = false;
            actor->ownsLines = true;
            actor->includeInFit = false;

            actor->lines = new Primitives::Lines3d(canvas, {
                .lines = &lines
            });

            actor->lines->color = {
                0.5f,
                0.5f,
                0.55f,
                0.2f
            };

            toolPreviewActor = new View3d::Actor();

            toolPreviewActor->visible = false;
            toolPreviewActor->selectable = false;
            toolPreviewActor->ownsMesh = true;
            toolPreviewActor->ownsTriangles = true;
            toolPreviewActor->includeInFit = false;

            toolPreviewActor->mesh = new Primitives::Mesh3d(canvas, {
                .triangles = &toolPreviewTriangles
            });

            toolPreviewActor->mesh->color = {
                0.92f,
                0.78f,
                0.35f,
                0.85f
            };
        }

        void destroy() {

            delete actor;
            actor = nullptr;

            delete toolPreviewActor;
            toolPreviewActor = nullptr;

            lines.clear();
            toolPreviewTriangles.clear();
        }

        // State
        //--------------------------------------------------

        void clear() {

            lines.clear();
            toolPreviewTriangles.clear();

            if (actor && actor->lines) {
                actor->visible = false;
                actor->lines->dirty = true;
            }

            if (toolPreviewActor && toolPreviewActor->mesh) {
                toolPreviewActor->visible = false;
                toolPreviewActor->mesh->dirty = true;
            }
        }

        void sync(
            Cam::App::MaterialState* state,
            double previewProgress = 1.0
        ) {

            syncPathLines(state, previewProgress);
            syncToolPreview(state, previewProgress);
        }

        void syncPathLines(
            Cam::App::MaterialState* state,
            double previewProgress
        ) {

            if (!actor || !actor->lines) { return; }

            lines.clear();

            if (!state || !state->hasToolPath) {
                actor->visible = false;
                actor->lines->dirty = true;
                return;
            }

            state->toolPath.buildLineSegments(lines, previewProgress);

            actor->visible = !lines.empty();
            actor->lines->dirty = true;
        }

        void syncToolPreview(
            Cam::App::MaterialState* state,
            double previewProgress
        ) {

            if (!toolPreviewActor || !toolPreviewActor->mesh) { return; }

            toolPreviewTriangles.clear();

            if (!state || !state->hasToolPath) {
                toolPreviewActor->visible = false;
                toolPreviewActor->mesh->dirty = true;
                return;
            }

            const Cam::App::ToolPath& path = state->toolPath;

            if (
                path.toolDiameter <= 0.0 ||
                path.toolLength <= 0.0 ||
                path.points.empty()
            ) {
                toolPreviewActor->visible = false;
                toolPreviewActor->mesh->dirty = true;
                return;
            }

            Cam::App::ToolPathPoint sample = {};

            if (!path.sampleAtProgress(previewProgress, sample)) {
                toolPreviewActor->visible = false;
                toolPreviewActor->mesh->dirty = true;
                return;
            }

            const float radius = float(path.toolDiameter * 0.5);
            const float length = float(path.toolLength);

            buildToolCylinderMesh(
                toolPreviewTriangles,
                sample.position,
                sample.toolDirection,
                radius,
                length,
                toolPreviewActor->mesh->color
            );

            toolPreviewActor->visible = !toolPreviewTriangles.empty();
            toolPreviewActor->mesh->dirty = true;
        }

    private:

        static void orthonormalFrameFromAxis(
            const Pos3& axisIn,
            Pos3& uOut,
            Pos3& vOut
        ) {

            const Pos3 axis = axisIn.normalized();

            const Pos3 reference = (
                std::fabs(axis.z) < 0.9f
                    ? Pos3(0.0f, 0.0f, 1.0f)
                    : Pos3(1.0f, 0.0f, 0.0f)
            );

            uOut = reference.cross(axis);

            const float uLen = uOut.pythag();

            if (uLen <= 1e-6f) {
                uOut = { 1.0f, 0.0f, 0.0f };
            }
            else {
                uOut /= uLen;
            }

            vOut = axis.cross(uOut).normalized();
        }

        static void appendTriangle(
            std::vector<Vertex3>& triangles,
            Pos3 a,
            Pos3 b,
            Pos3 c,
            const Color& color
        ) {
            triangles.push_back({ a.x, a.y, a.z, color });
            triangles.push_back({ b.x, b.y, b.z, color });
            triangles.push_back({ c.x, c.y, c.z, color });
        }

        // Solid cylinder with flat caps. Tip at tipPosition; body extends length mm
        // along -toolDirection (toward the spindle). Path toolDirection points into
        // the cut, so the displayed axis is flipped for preview geometry.
        static void buildToolCylinderMesh(
            std::vector<Vertex3>& triangles,
            const Pos3& tipPosition,
            const Pos3& toolDirectionIn,
            float radius,
            float length,
            const Color& color,
            int sides = 24
        ) {

            if (radius <= 0.0f || length <= 0.0f || sides < 3) { return; }

            const Pos3 axis = (toolDirectionIn * -1.0f).normalized();
            const Pos3 baseCenter = tipPosition - axis * length;

            Pos3 u = {};
            Pos3 v = {};
            orthonormalFrameFromAxis(axis, u, v);

            std::vector<Pos3> baseRing(static_cast<size_t>(sides));
            std::vector<Pos3> tipRing(static_cast<size_t>(sides));

            const float twoPi = 6.28318530718f;

            for (int i = 0; i < sides; i++) {
                const float angle = twoPi * float(i) / float(sides);
                const Pos3 offset = u * std::cos(angle) * radius + v * std::sin(angle) * radius;

                baseRing[static_cast<size_t>(i)] = baseCenter + offset;
                tipRing[static_cast<size_t>(i)] = tipPosition + offset;
            }

            for (int i = 0; i < sides; i++) {
                const int next = (i + 1) % sides;

                const Pos3& b0 = baseRing[static_cast<size_t>(i)];
                const Pos3& b1 = baseRing[static_cast<size_t>(next)];
                const Pos3& t0 = tipRing[static_cast<size_t>(i)];
                const Pos3& t1 = tipRing[static_cast<size_t>(next)];

                appendTriangle(triangles, b0, b1, t0, color);
                appendTriangle(triangles, b1, t1, t0, color);
            }

            for (int i = 0; i < sides; i++) {
                const int next = (i + 1) % sides;

                appendTriangle(
                    triangles,
                    baseCenter,
                    baseRing[static_cast<size_t>(next)],
                    baseRing[static_cast<size_t>(i)],
                    color
                );

                appendTriangle(
                    triangles,
                    tipPosition,
                    tipRing[static_cast<size_t>(i)],
                    tipRing[static_cast<size_t>(next)],
                    color
                );
            }
        }
    };
}
