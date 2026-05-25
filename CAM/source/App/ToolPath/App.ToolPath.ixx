module;

#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <cstddef>

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

    // One slice-layer feature before global linking.
    struct ToolPathFeature {
        std::vector<Pos3> approach;
        std::vector<Pos3> abscond;
        std::vector<ToolPathPoint> points;
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
        std::vector<ToolPathFeature> features;
        std::vector<ToolPathPoint> points;
        bool computed = false;

        // State
        //--------------------------------------------------

        ToolPath() = default;
        ToolPath(const ToolPath&) = delete;
        ToolPath& operator=(const ToolPath&) = delete;
        ToolPath(ToolPath&&) = default;
        ToolPath& operator=(ToolPath&&) = default;

        void clearPathData() {
            features.clear();
            points.clear();
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

        void appendWorldPoint(
            std::vector<ToolPathPoint>& out,
            const Pos3& position,
            double& t,
            bool rapid = false,
            bool cutting = true
        ) {
            out.push_back({
                .position = position,
                .t = t,
                .rapid = rapid,
                .cutting = cutting
            });

            t += 1.0;
        }

        void addPoint(const Pos& uv, float depth, const Slicer::Strategy::CutFrame& frame, double& t, bool rapid = false, bool cutting = true) {
            appendWorldPoint(points, frame.uvToWorld(uv, depth), t, rapid, cutting);
        }

        void addFeaturePoint(
            ToolPathFeature& feature,
            const Pos& uv,
            float depth,
            const Slicer::Strategy::CutFrame& frame,
            double& t,
            bool rapid = false,
            bool cutting = true
        ) {
            appendWorldPoint(feature.points, frame.uvToWorld(uv, depth), t, rapid, cutting);
        }

        void addSegmentPoints(
            std::vector<ToolPathPoint>& out,
            const Segment& segment,
            float depth,
            const Slicer::Strategy::CutFrame& frame,
            double& t,
            int samples = 24
        ) {
            if (segment.kind == Segment::Kind::Line) {
                appendWorldPoint(out, frame.uvToWorld(segment.start(), depth), t);
                appendWorldPoint(out, frame.uvToWorld(segment.end(), depth), t);
                return;
            }

            Pos last = segment.at(0.0f);

            for (int i = 1; i <= samples; i++) {
                Pos p = segment.at(float(i) / float(samples));

                appendWorldPoint(out, frame.uvToWorld(last, depth), t);
                appendWorldPoint(out, frame.uvToWorld(p, depth), t);

                last = p;
            }
        }

        void addSegmentPoints(const Segment& segment, float depth, const Slicer::Strategy::CutFrame& frame, double& t, int samples = 24) {
            addSegmentPoints(points, segment, depth, frame, t, samples);
        }

        void buildFeatureCuts(
            const Slicer::Strategy::LayerPath& layer,
            const Slicer::Strategy::CutFrame& frame,
            ToolPathFeature& feature,
            double& t
        ) {
            if (!layer.points.empty()) {
                for (const Pos& p : layer.points) {
                    addFeaturePoint(feature, p, layer.z, frame, t);
                }

                return;
            }

            for (const Segment& segment : layer.segments) {
                addSegmentPoints(feature.points, segment, layer.z, frame, t);
            }
        }

        void flattenFeatureToPoints(const ToolPathFeature& feature, double& t) {

            for (const Pos3& position : feature.approach) {
                appendWorldPoint(points, position, t, true, false);
            }

            for (const ToolPathPoint& point : feature.points) {
                appendWorldPoint(points, point.position, t, point.rapid, point.cutting);
            }

            for (const Pos3& position : feature.abscond) {
                appendWorldPoint(points, position, t, true, false);
            }
        }

        void buildPointsFromStrategy(const Slicer::Strategy::Strategy& strategyImpl, const Slicer::Strategy::CutFrame& frame) {
            features.clear();
            points.clear();

            double t = 0.0;

            for (const Slicer::Strategy::LayerPath& layer : strategyImpl.paths()) {

                ToolPathFeature feature;

                feature.approach = layer.approach;
                feature.abscond = layer.abscond;

                buildFeatureCuts(layer, frame, feature, t);
                flattenFeatureToPoints(feature, t);

                features.push_back(std::move(feature));
            }
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

            computed = !points.empty();

            dbg(
                "[ToolPath] Done. slices=%zu paths=%zu features=%zu points=%zu computed=%i",
                strategyImpl.slices().size(),
                strategyImpl.paths().size(),
                features.size(),
                points.size(),
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

            Color cutColor = { 1.0f, 0.0f, 1.0f, 1.0f };
            Color rapidColor = { 0.6f, 0.0f, 1.0f, 0.35f };

            for (const ToolPathFeature& feature : features) {

                const std::vector<ToolPathPoint>& featurePoints = feature.points;

                if (featurePoints.size() < 2) { continue; }

                for (size_t i = 0; i + 1 < featurePoints.size(); i++) {
                    const Pos3& a = featurePoints[i].position;
                    const Pos3& b = featurePoints[i + 1].position;

                    Color color = featurePoints[i + 1].rapid ? rapidColor : cutColor;

                    lines.push_back({ a.x, a.y, a.z, color });
                    lines.push_back({ b.x, b.y, b.z, color });
                }
            }
        }

        void buildLinkLineSegments(std::vector<Vertex3>& lines) const {

            Color linkColor = { 0.25f, 0.85f, 1.0f, 1.0f };

            auto appendPolyline = [&](const std::vector<Pos3>& path) {
                if (path.size() < 2) { return; }

                for (size_t i = 0; i + 1 < path.size(); i++) {
                    const Pos3& a = path[i];
                    const Pos3& b = path[i + 1];

                    lines.push_back({ a.x, a.y, a.z, linkColor });
                    lines.push_back({ b.x, b.y, b.z, linkColor });
                }
            };

            for (const ToolPathFeature& feature : features) {
                appendPolyline(feature.approach);
                appendPolyline(feature.abscond);
            }
        }

    private:

        std::optional<StrategyInstance> strategyInstance;
    };
}
