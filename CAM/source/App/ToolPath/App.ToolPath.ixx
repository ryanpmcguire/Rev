module;

#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <cstddef>
#include <cmath>
#include <algorithm>

#include <dbg.hpp>

export module Cam.App.ToolPath;

import Rev.Core.Vertex3;
import Rev.Core.Color;
import Rev.Core.Pos;
import Rev.Core.Pos3;

import Cam.App.Model;
import Cam.App.Tool;
import Cam.App.Slicer.Strategy.Slice.Segment2;

import Cam.App.Slicer.Strategy.Strategy;
import Cam.App.Slicer.Strategy.CutFrame;
import Cam.App.Slicer.Strategy.Strategies.Bore;
import Cam.App.Slicer.Strategy.Strategies.Profile;
import Cam.App.Slicer.Strategy.Strategies.Hatch;

export namespace Cam::App {

    using namespace Rev::Core;
    using Segment = Slicer::Strategy::Slice::Segment;

    struct ToolPathPoint {
        Pos3 position = {};
        double t = 0.0;
        bool rapid = false;
        bool cutting = true;
    };

    // Computed tool motion for one material state.
    struct ToolPath {

        using Bore = Slicer::Strategy::Strategies::Bore;
        using Profile = Slicer::Strategy::Strategies::Profile;
        using Hatch = Slicer::Strategy::Strategies::Hatch;

        using StrategyInstance = std::variant<Bore, Profile, Hatch>;

        // Settings
        std::string toolName = "";
        std::string strategy = Hatch::name();
        bool strategyAuto = true;

        double stepDown = 1.0;
        double feedRate = 1000.0;
        double stepover = 0.25;

        // Slicing frame: depth steps along sliceAxis; 2D work stays in (u,v).
        Pos3 sliceAxis = { 0.0f, 0.0f, 1.0f };
        Pos3 sliceOrigin = {};

        static constexpr size_t NoSliceFaceId = static_cast<size_t>(-1);
        size_t sliceFaceId = NoSliceFaceId;

        // Result
        std::vector<ToolPathPoint> points;
        std::vector<ToolPathPoint> axis;
        bool computed = false;

        static constexpr float axisDebugLength = 50.0f;

        // State
        //--------------------------------------------------

        ToolPath() = default;
        ToolPath(const ToolPath&) = delete;
        ToolPath& operator=(const ToolPath&) = delete;
        ToolPath(ToolPath&&) = default;
        ToolPath& operator=(ToolPath&&) = default;

        void clearPathData() {
            points.clear();
            axis.clear();
            computed = false;
            strategyInstance.reset();
        }

        void clear() {
            toolName.clear();
            clearPathData();
        }

        bool empty() const { return points.empty(); }
        size_t size() const { return points.size(); }

        bool hasSliceFace() const {
            return sliceFaceId != NoSliceFaceId;
        }

        void clearSlicePlane() {
            sliceAxis = { 0.0f, 0.0f, 1.0f };
            sliceFaceId = NoSliceFaceId;
        }

        void setSlicePlane(size_t faceId, const Pos3& axis) {
            sliceAxis = axis;
            sliceFaceId = faceId;
        }

        const Slicer::Strategy::Strategy* strategyResult() const {
            if (!strategyInstance) { return nullptr; }

            return std::visit(
                [](const auto& s) -> const Slicer::Strategy::Strategy* {
                    return &s;
                },
                *strategyInstance
            );
        }

        // Strategy detection
        //--------------------------------------------------

        static std::string detectStrategy(const Model& model) {

            if (Bore::detect(model)) { return Bore::name(); }
            if (Profile::detect(model)) { return Profile::name(); }
            if (Hatch::detect(model)) { return Hatch::name(); }

            return Hatch::name();
        }

        Slicer::Strategy::CutFrame cutFrame() const {
            return Slicer::Strategy::CutFrame::fromAxis(sliceAxis, sliceOrigin);
        }

        // Point building
        //--------------------------------------------------

        void addPoint(const Pos& uv, float depth, const Slicer::Strategy::CutFrame& frame, double& t, bool rapid = false, bool cutting = true) {
            points.push_back({
                .position = frame.uvToWorld(uv, depth),
                .t = t,
                .rapid = rapid,
                .cutting = cutting
            });

            t += 1.0;
        }

        void addSegmentPoints(const Segment& segment, float depth, const Slicer::Strategy::CutFrame& frame, double& t, int samples = 24) {
            if (segment.kind == Segment::Kind::Line) {
                addPoint(segment.start(), depth, frame, t);
                addPoint(segment.end(), depth, frame, t);
                return;
            }

            Pos last = segment.at(0.0f);

            for (int i = 1; i <= samples; i++) {
                Pos p = segment.at(float(i) / float(samples));

                addPoint(last, depth, frame, t);
                addPoint(p, depth, frame, t);

                last = p;
            }
        }

        void buildPointsFromStrategy(const Slicer::Strategy::Strategy& strategyImpl, const Slicer::Strategy::CutFrame& frame) {
            points.clear();

            double t = 0.0;

            for (const Slicer::Strategy::LayerPath& layer : strategyImpl.paths()) {
                if (!layer.points.empty()) {
                    for (const Pos& p : layer.points) {
                        addPoint(p, layer.z, frame, t);
                    }

                    continue;
                }

                for (const Segment& segment : layer.segments) {
                    addSegmentPoints(segment, layer.z, frame, t);
                }
            }
        }

        void buildAxisFromStrategy(
            const Slicer::Strategy::Strategy& strategyImpl,
            const Slicer::Strategy::CutFrame& frame
        ) {
            axis.clear();

            const std::vector<Slicer::Strategy::LayerPath>& paths = strategyImpl.paths();

            if (paths.empty()) { return; }

            float minDepth = paths.front().z;

            for (const Slicer::Strategy::LayerPath& layer : paths) {
                minDepth = std::min(minDepth, layer.z);
            }

            Pos uvSum = {};
            size_t uvCount = 0;

            auto addUv = [&](const Pos& uv) {
                uvSum.x += uv.x;
                uvSum.y += uv.y;
                uvCount += 1;
            };

            for (const Slicer::Strategy::LayerPath& layer : paths) {
                if (std::abs(layer.z - minDepth) > 1e-4f) { continue; }

                for (const Pos& uv : layer.points) {
                    addUv(uv);
                }

                for (const Segment& segment : layer.segments) {
                    addUv(segment.start());
                    addUv(segment.end());
                }
            }

            Pos uv = uvCount > 0
                ? Pos { uvSum.x / float(uvCount), uvSum.y / float(uvCount) }
                : Pos {};

            Pos3 origin = frame.uvToWorld(uv, minDepth);
            Pos3 direction = frame.axis.normalized();
            Pos3 tip = origin + direction * axisDebugLength;

            axis.push_back({
                .position = origin,
                .t = 0.0,
                .rapid = false,
                .cutting = false
            });

            axis.push_back({
                .position = tip,
                .t = 1.0,
                .rapid = false,
                .cutting = false
            });
        }

        // Compute
        //--------------------------------------------------

        bool compute(Model& toCarve, Model& toAvoid, const Tool& tool) {
            clearPathData();

            toolName = tool.name;

            if (strategyAuto) {

                const std::string detected = detectStrategy(toCarve);

                if (detected != strategy) {
                    dbg(
                        "[ToolPath] Auto-detected strategy: %s -> %s",
                        strategy.c_str(),
                        detected.c_str()
                    );

                    strategy = detected;
                }
            }

            dbg(
                "[ToolPath] Computing toolpath with tool \"%s\" strategy=%s (auto=%i)",
                toolName.c_str(),
                strategy.c_str(),
                int(strategyAuto)
            );

            const Slicer::Strategy::CutFrame frame = cutFrame();

            Slicer::Strategy::StrategyContext ctx {
                .positive = &toCarve,
                .negative = &toAvoid,
                .tool = &tool,
                .stepDown = static_cast<float>(stepDown),
                .stepover = static_cast<float>(stepover),
                .frame = frame
            };

            if (strategy == Bore::name()) { strategyInstance = Bore {}; }
            else if (strategy == Profile::name()) { strategyInstance = Profile {}; }
            else {
                strategy = Hatch::name();
                strategyInstance = Hatch {};
            }

            std::visit([&ctx](auto& s) {
                Slicer::Strategy::Strategy::run(s, ctx);
            }, *strategyInstance);

            const Slicer::Strategy::Strategy& strategyImpl =
                *strategyResult();

            buildPointsFromStrategy(strategyImpl, frame);
            buildAxisFromStrategy(strategyImpl, frame);

            computed = !points.empty();

            dbg(
                "[ToolPath] Done. slices=%zu paths=%zu points=%zu axis=%zu computed=%i",
                strategyImpl.slices().size(),
                strategyImpl.paths().size(),
                points.size(),
                axis.size(),
                int(computed)
            );

            return computed;
        }

        bool compute(Model& toCarve, const Tool& tool) {
            return compute(toCarve, toCarve, tool);
        }

        bool compute(Model& toCarve) {
            return compute(toCarve, Tool::GodTool());
        }

        bool compute(Model& toCarve, Model& toAvoid) {
            return compute(toCarve, toAvoid, Tool::GodTool());
        }

        bool computeFromDelta(Model& deltaModel, Model& remainingModel, const Tool& tool) {
            return compute(deltaModel, remainingModel, tool);
        }

        // Rendering
        //--------------------------------------------------

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

        void buildAxisLineSegments(std::vector<Vertex3>& lines) const {

            if (axis.size() < 2) { return; }

            Color axisColor = { 0.55f, 0.82f, 1.0f, 1.0f };

            for (size_t i = 0; i + 1 < axis.size(); i++) {
                const Pos3& a = axis[i].position;
                const Pos3& b = axis[i + 1].position;

                lines.push_back({ a.x, a.y, a.z, axisColor });
                lines.push_back({ b.x, b.y, b.z, axisColor });
            }
        }

    private:

        std::optional<StrategyInstance> strategyInstance;
    };
}
