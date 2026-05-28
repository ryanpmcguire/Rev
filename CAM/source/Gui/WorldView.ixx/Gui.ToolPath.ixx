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

        std::vector<Vertex3> lines;

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
        }

        void destroy() {

            delete actor;
            actor = nullptr;

            lines.clear();
        }

        // State
        //--------------------------------------------------

        void clear() {

            lines.clear();

            if (!actor || !actor->lines) { return; }

            actor->visible = false;
            actor->lines->dirty = true;
        }

        void sync(
            Cam::App::MaterialState* state,
            double previewProgress = 1.0
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

        // Shared tool preview mesh (one cylinder in the world view).
        //--------------------------------------------------

        static bool syncToolPreviewMesh(
            std::vector<Vertex3>& triangles,
            Cam::App::MaterialState* state,
            double previewProgress,
            const Color& color
        ) {

            triangles.clear();

            if (!state || !state->hasToolPath) { return false; }

            const Cam::App::ToolPath& path = state->toolPath;

            if (
                path.toolDiameter <= 0.0 ||
                path.toolLength <= 0.0 ||
                path.points.empty()
            ) {
                return false;
            }

            Cam::App::ToolPathPoint sample = {};

            if (!path.sampleAtProgress(previewProgress, sample)) {
                return false;
            }

            const float radius = float(path.toolDiameter * 0.5);
            const float length = float(path.toolLength);

            buildToolCylinderMesh(
                triangles,
                sample.position,
                sample.toolDirection,
                radius,
                length,
                color
            );

            return !triangles.empty();
        }

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
    };
}
