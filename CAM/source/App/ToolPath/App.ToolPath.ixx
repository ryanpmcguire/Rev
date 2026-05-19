module;

#include <vector>
#include <string>
#include <cstddef>
#include <limits>
#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

export module Cam.App.ToolPath;

import Rev.Core.Vertex3;
import Rev.Core.Color;

import Cam.App.Model;

export namespace Cam::App {

    struct Tool {

        enum class Kind {
            Cylinder
        };

        Kind kind = Kind::Cylinder;

        std::string name = "1mm x 100mm God Tool";

        double diameter = 1.0;
        double radius = 0.5;
        double length = 100.0;

        glm::vec3 axis = { 0.0f, 0.0f, 1.0f };

        static Tool GodTool() {

            Tool tool;

            tool.kind = Kind::Cylinder;
            tool.name = "1mm x 100mm God Tool";

            tool.diameter = 1.0;
            tool.radius = 0.5;
            tool.length = 100.0;

            tool.axis = { 0.0f, 0.0f, 1.0f };

            return tool;
        }
    };

    struct ToolPathPoint {

        glm::vec3 position = { 0.0f, 0.0f, 0.0f };

        double t = 0.0;

        bool rapid = false;
        bool cutting = true;
    };

    struct ToolPathSlice {

        float z = 0.0f;

        glm::vec2 min = { 0.0f, 0.0f };
        glm::vec2 max = { 0.0f, 0.0f };

        std::vector<glm::vec2> intersections;
        std::vector<glm::vec2> points;

        bool valid = false;

        void clear() {

            intersections.clear();
            points.clear();

            min = { 0.0f, 0.0f };
            max = { 0.0f, 0.0f };

            valid = false;
        }

        void addIntersection(glm::vec2 p) {

            intersections.push_back(p);

            if (!valid) {
                min = p;
                max = p;
                valid = true;
                return;
            }

            min = glm::min(min, p);
            max = glm::max(max, p);
        }

        bool crossesZ(
            glm::vec3 a,
            glm::vec3 b
        ) const {
            float zMin = std::min(a.z, b.z);
            float zMax = std::max(a.z, b.z);

            return (
                z >= zMin &&
                z <= zMax &&
                std::abs(a.z - b.z) > 1e-6f
            );
        }

        void addEdgeIntersection(
            glm::vec3 a,
            glm::vec3 b
        ) {
            if (!crossesZ(a, b)) { return; }

            float t = (z - a.z) / (b.z - a.z);

            if (t < 0.0f || t > 1.0f) { return; }

            glm::vec3 p = a + (b - a) * t;

            addIntersection({ p.x, p.y });
        }

        void collectFromTriangle(
            glm::vec3 a,
            glm::vec3 b,
            glm::vec3 c
        ) {
            addEdgeIntersection(a, b);
            addEdgeIntersection(b, c);
            addEdgeIntersection(c, a);
        }

        void solve(
            const Tool& tool,
            bool reverse = false
        ) {
            points.clear();

            if (!valid) { return; }
            if (intersections.size() < 2) { return; }

            float spacing = static_cast<float>(tool.diameter);

            if (spacing <= 0.0f) { spacing = 1.0f; }

            // Keep the tool center inside the approximate slice bounds.
            float x0 = min.x + static_cast<float>(tool.radius);
            float x1 = max.x - static_cast<float>(tool.radius);
            float y0 = min.y + static_cast<float>(tool.radius);
            float y1 = max.y - static_cast<float>(tool.radius);

            if (x1 < x0 || y1 < y0) { return; }

            size_t row = 0;

            for (float y = y0; y <= y1 + 1e-4f; y += spacing) {

                bool leftToRight = ((row % 2) == 0);

                if (reverse) { leftToRight = !leftToRight; }

                if (leftToRight) {
                    points.push_back({ x0, y });
                    points.push_back({ x1, y });
                }

                else {
                    points.push_back({ x1, y });
                    points.push_back({ x0, y });
                }

                row += 1;
            }
        }
    };

    struct ToolPath {

        Tool tool = Tool::GodTool();

        std::vector<ToolPathSlice> slices;
        std::vector<ToolPathPoint> points;

        bool computed = false;

        double stepDown = 1.0;

        void clear() {

            slices.clear();
            points.clear();

            computed = false;
        }

        bool empty() const {
            return points.empty();
        }

        size_t size() const {
            return points.size();
        }

        bool boundsFromModel(
            const Model& model,
            glm::vec3& min,
            glm::vec3& max
        ) {
            if (!model.loaded) { return false; }
            if (model.render.triangles.empty()) { return false; }

            bool valid = false;

            for (const Rev::Core::Vertex3& v : model.render.triangles) {

                glm::vec3 p = { v.x, v.y, v.z };

                if (!valid) {
                    min = p;
                    max = p;
                    valid = true;
                    continue;
                }

                min = glm::min(min, p);
                max = glm::max(max, p);
            }

            return valid;
        }

        void collectSliceIntersections(
            const Model& model,
            ToolPathSlice& slice
        ) {
            const std::vector<Rev::Core::Vertex3>& tris =
                model.render.triangles;

            for (size_t i = 0; i + 2 < tris.size(); i += 3) {

                const Rev::Core::Vertex3& va = tris[i];
                const Rev::Core::Vertex3& vb = tris[i + 1];
                const Rev::Core::Vertex3& vc = tris[i + 2];

                glm::vec3 a = { va.x, va.y, va.z };
                glm::vec3 b = { vb.x, vb.y, vb.z };
                glm::vec3 c = { vc.x, vc.y, vc.z };

                slice.collectFromTriangle(a, b, c);
            }
        }

        void buildPointsFromSlices() {

            points.clear();

            double t = 0.0;

            for (ToolPathSlice& slice : slices) {

                for (glm::vec2& p : slice.points) {

                    points.push_back({
                        .position = { p.x, p.y, slice.z },
                        .t = t,
                        .rapid = false,
                        .cutting = true
                    });

                    t += 1.0;
                }
            }
        }

        bool computeFromDelta(
            const Model& deltaModel,
            Tool tool = Tool::GodTool()
        ) {
            clear();

            this->tool = tool;

            glm::vec3 min;
            glm::vec3 max;

            if (!boundsFromModel(deltaModel, min, max)) {
                return false;
            }

            float dz = static_cast<float>(stepDown);

            if (dz <= 0.0f) { dz = 1.0f; }

            bool reverse = false;

            for (float z = min.z; z <= max.z + 1e-4f; z += dz) {

                ToolPathSlice slice;

                slice.z = z;

                collectSliceIntersections(
                    deltaModel,
                    slice
                );

                slice.solve(
                    this->tool,
                    reverse
                );

                if (!slice.points.empty()) {
                    slices.push_back(slice);
                    reverse = !reverse;
                }
            }

            buildPointsFromSlices();

            computed = !points.empty();

            return computed;
        }

        void buildLineSegments(
            std::vector<Rev::Core::Vertex3>& lines
        ) const {
            lines.clear();

            if (points.size() < 2) { return; }

            Rev::Core::Color cutColor = {
                1.0f,
                0.0f,
                1.0f,
                1.0f
            };

            Rev::Core::Color rapidColor = {
                0.6f,
                0.0f,
                1.0f,
                0.35f
            };

            for (size_t i = 0; i + 1 < points.size(); i++) {

                glm::vec3 a = points[i].position;
                glm::vec3 b = points[i + 1].position;

                Rev::Core::Color color = (
                    points[i + 1].rapid
                    ? rapidColor
                    : cutColor
                );

                lines.push_back({ a.x, a.y, a.z, color });
                lines.push_back({ b.x, b.y, b.z, color });
            }
        }
    };
}