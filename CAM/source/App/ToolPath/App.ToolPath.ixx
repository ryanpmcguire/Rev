module;

#include <vector>
#include <cstddef>
#include <algorithm>

#include <dbg.hpp>

export module Cam.App.ToolPath;

import Rev.Core.Vertex3;
import Rev.Core.Color;
import Rev.Core.Pos;
import Rev.Core.Pos3;

import Cam.App.Model;
import Cam.App.Tool;
import Cam.App.Slice2d;
import Cam.App.Geometry.Segment;

export namespace Cam::App {

    using namespace Rev::Core;

    struct ToolPathPoint {
        Pos3 position = {};
        double t = 0.0;
        bool rapid = false;
        bool cutting = true;
    };

    struct ToolPath {
        Tool tool = Tool::GodTool();

        Slice2d::Strategy strategy = Slice2d::Strategy::Profile;

        std::vector<Slice2d> slices;
        std::vector<ToolPathPoint> points;

        bool computed = false;

        double stepDown = 1.0;

        void clear() {
            slices.clear();
            points.clear();
            computed = false;
        }

        bool empty() const { return points.empty(); }
        size_t size() const { return points.size(); }

        bool boundsFromModel(const Model& model, Pos3& min, Pos3& max) const {
            if (!model.loaded) { return false; }
            if (model.render.triangles.empty()) { return false; }

            bool valid = false;

            for (const Vertex3& v : model.render.triangles) {
                if (!valid) {
                    min = v;
                    max = v;
                    valid = true;
                    continue;
                }

                min = Pos3::min(min, v);
                max = Pos3::max(max, v);
            }

            return valid;
        }

        bool buildSlice(Model& model, float z, Slice2d& slice) const {
            slice = Slice2d::FromModel(model, z, strategy, tool);
            return !slice.empty();
        }

        void addPoint(const Pos& p, float z, double& t, bool rapid = false, bool cutting = true) {
            points.push_back({
                .position = { p.x, p.y, z },
                .t = t,
                .rapid = rapid,
                .cutting = cutting
            });

            t += 1.0;
        }

        void addSegmentPoints(const Segment& segment, float z, double& t, int samples = 24) {
            if (segment.kind == Segment::Kind::Line) {
                addPoint(segment.start(), z, t);
                addPoint(segment.end(), z, t);
                return;
            }

            Pos last = segment.at(0.0f);

            for (int i = 1; i <= samples; i++) {
                Pos p = segment.at(float(i) / float(samples));

                addPoint(last, z, t);
                addPoint(p, z, t);

                last = p;
            }
        }

        void buildPointsFromSlices() {
            points.clear();

            double t = 0.0;

            for (const Slice2d& slice : slices) {
                if (slice.hasPointPath()) {
                    for (const Pos& p : slice.points) {
                        addPoint(p, slice.z, t);
                    }

                    continue;
                }

                for (const Segment& segment : slice.paths) {
                    addSegmentPoints(segment, slice.z, t);
                }
            }
        }

        bool compute(Model& toCarve, Tool& tool) {
            clear();

            this->tool = tool;

            dbg("[ToolPath] Computing toolpath");

            Pos3 min;
            Pos3 max;

            if (!boundsFromModel(toCarve, min, max)) {
                dbg("[ToolPath] Failed: no carve bounds");
                return false;
            }

            dbg(
                "[ToolPath] Carve bounds min=(%.3f %.3f %.3f), max=(%.3f %.3f %.3f)",
                min.x, min.y, min.z,
                max.x, max.y, max.z
            );

            float dz = static_cast<float>(stepDown);
            if (dz <= 0.0f) { dz = 1.0f; }

            size_t attempted = 0;
            size_t solved = 0;

            for (float z = min.z; z <= max.z + 1e-4f; z += dz) {
                attempted += 1;

                Slice2d slice;

                if (!buildSlice(toCarve, z, slice)) {
                    dbg("[ToolPath] z=%.3f: no slice path", z);
                    continue;
                }

                slices.push_back(slice);
                solved += 1;
            }

            buildPointsFromSlices();

            computed = !points.empty();

            dbg(
                "[ToolPath] Done. attempted=%zu solved=%zu slices=%zu points=%zu computed=%i",
                attempted,
                solved,
                slices.size(),
                points.size(),
                int(computed)
            );

            return computed;
        }

        bool compute(Model& toCarve) {
            Tool tool = Tool::GodTool();
            return compute(toCarve, tool);
        }

        bool compute(Model& toCarve, Model& toAvoid, Tool& tool) {
            // toAvoid will come back once Slice2d owns protected/avoid classification.
            return compute(toCarve, tool);
        }

        bool compute(Model& toCarve, Model& toAvoid) {
            Tool tool = Tool::GodTool();
            return compute(toCarve, toAvoid, tool);
        }

        bool computeFromDelta(Model& deltaModel, Model& remainingModel, Tool tool = Tool::GodTool()) {
            return compute(deltaModel, remainingModel, tool);
        }

        void buildLineSegments(std::vector<Vertex3>& lines) const {
            lines.clear();

            if (points.size() < 2) { return; }

            Color cutColor = { 1.0f, 0.0f, 1.0f, 1.0f };
            Color rapidColor = { 0.6f, 0.0f, 1.0f, 0.35f };

            for (size_t i = 0; i + 1 < points.size(); i++) {
                const Pos3& a = points[i].position;
                const Pos3& b = points[i + 1].position;

                Color color = points[i + 1].rapid ? rapidColor : cutColor;

                lines.push_back({ a.x, a.y, a.z, color });
                lines.push_back({ b.x, b.y, b.z, color });
            }
        }
    };
}