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
        Pos3 toolDirection = { 0.0f, 0.0f, 1.0f }; // normalized slice axis at this point
        double spindleSpeed = 0.0; // RPM; placeholder until spindle model exists
        double t = 0.0; // seconds from path start
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

        // Cached from the tool used at last compute (for preview geometry).
        double toolDiameter = 0.0;
        double toolLength = 0.0;

        // Slicing frame: depth steps along sliceAxis; 2D work stays in (u,v).
        Pos3 sliceAxis = { 0.0f, 0.0f, 1.0f };
        Pos3 sliceOrigin = {};

        static constexpr size_t NoSliceFaceId = static_cast<size_t>(-1);
        size_t sliceFaceId = NoSliceFaceId;

        // Result
        std::vector<ToolPathPoint> points;
        std::vector<ToolPathPoint> axis;
        bool computed = false;
        size_t linkedPointCount = 0;

        static constexpr float linkRetractDistance = 10.0f;
        static constexpr double travelSpeedMmPerSec = 1.0;

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
            linkedPointCount = 0;
            toolDiameter = 0.0;
            toolLength = 0.0;
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

        void addWorldPoint(
            const Pos3& position,
            bool rapid = false,
            bool cutting = true
        ) {
            points.push_back(makePoint(position, rapid, cutting));
        }

        void addPoint(
            const Pos& uv,
            float depth,
            const Slicer::Strategy::CutFrame& frame,
            bool rapid = false,
            bool cutting = true
        ) {
            addWorldPoint(frame.uvToWorld(uv, depth), rapid, cutting);
        }

        void assignPointTimes(double speedMmPerSec = travelSpeedMmPerSec) {

            if (points.empty()) { return; }

            points.front().t = 0.0;

            if (speedMmPerSec <= 0.0) { return; }

            for (size_t i = 1; i < points.size(); i++) {
                const float distance = points[i - 1].position.distanceTo(points[i].position);

                points[i].t = points[i - 1].t + double(distance) / speedMmPerSec;
            }
        }

        // Generation appends in reverse execution order; flip for display and preview.
        void reversePointsForForwardDisplay() {

            if (points.size() < 2) { return; }

            std::reverse(points.begin(), points.end());
            assignPointTimes();
        }

        void addSegmentPoints(
            const Segment& segment,
            float depth,
            const Slicer::Strategy::CutFrame& frame,
            int samples = 24
        ) {
            if (segment.kind == Segment::Kind::Line) {
                addPoint(segment.start(), depth, frame);
                addPoint(segment.end(), depth, frame);
                return;
            }

            Pos last = segment.at(0.0f);

            for (int i = 1; i <= samples; i++) {
                Pos p = segment.at(float(i) / float(samples));

                addPoint(last, depth, frame);
                addPoint(p, depth, frame);

                last = p;
            }
        }

        void buildPointsFromStrategy(
            const Slicer::Strategy::Strategy& strategyImpl,
            const Slicer::Strategy::CutFrame& frame
        ) {
            points.clear();

            for (const Slicer::Strategy::LayerPath& layer : strategyImpl.paths()) {
                if (!layer.points.empty()) {
                    for (const Pos& p : layer.points) {
                        addPoint(p, layer.z, frame);
                    }

                    continue;
                }

                for (const Segment& segment : layer.segments) {
                    addSegmentPoints(segment, layer.z, frame);
                }
            }
        }

        bool computeAxisAnchor(
            const Slicer::Strategy::Strategy& strategyImpl,
            const Slicer::Strategy::CutFrame& frame,
            Pos3& anchorOut
        ) const {

            const std::vector<Slicer::Strategy::LayerPath>& paths = strategyImpl.paths();

            if (paths.empty()) { return false; }

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

            anchorOut = frame.uvToWorld(uv, minDepth);

            return true;
        }

        Pos3 linkSafePoint(
            const Slicer::Strategy::CutFrame& frame,
            const Pos3& anchor,
            const Model* referenceModel
        ) const {

            const float anchorDepth = frame.dotFromOrigin(anchor);
            const Pos anchorUv = frame.worldToUv(anchor, anchorDepth);

            float safeDepth = anchorDepth + linkRetractDistance;

            if (
                hasSliceFace() &&
                referenceModel &&
                sliceFaceId < referenceModel->faceCount()
            ) {
                const Pos3 faceRef = referenceModel->facePoint(sliceFaceId);
                const float faceDepth = frame.dotFromOrigin(faceRef);

                safeDepth = faceDepth + linkRetractDistance;
            }

            return frame.uvToWorld(anchorUv, safeDepth);
        }

        void addApproachRetractLinks(
            const Pos3& axisAnchor,
            const Slicer::Strategy::CutFrame& frame,
            const Model* referenceModel
        ) {

            if (points.empty()) { return; }

            const Pos3 offset = linkSafePoint(frame, axisAnchor, referenceModel);

            // Toolpath points are stored in reverse execution order.
            // Retract: axis offset -> first stored point (last real-life step).
            points.insert(points.begin(), makePoint(offset, true, false));

            // Approach: last stored point (first real-life step) -> axis offset.
            addWorldPoint(offset, true, false);
        }

        void buildAxisDebugLine(
            const Slicer::Strategy::CutFrame& frame,
            const Pos3& anchor,
            const Model* referenceModel
        ) {
            axis.clear();

            Pos3 tip = linkSafePoint(frame, anchor, referenceModel);

            axis.push_back(makePoint(anchor, false, false));
            axis.back().t = 0.0;

            axis.push_back(makePoint(tip, false, false));
            axis.back().t = 1.0;
        }

        // Compute
        //--------------------------------------------------

        bool compute(Model& toCarve, Model& toAvoid, const Tool& tool) {
            clearPathData();

            toolName = tool.name;
            toolDiameter = tool.diameter;
            toolLength = tool.length;

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

            Pos3 axisAnchor = {};
            const bool hasAxisAnchor = computeAxisAnchor(strategyImpl, frame, axisAnchor);

            if (hasAxisAnchor && !points.empty()) {
                addApproachRetractLinks(axisAnchor, frame, &toAvoid);
            }

            reversePointsForForwardDisplay();

            if (hasAxisAnchor) {
                buildAxisDebugLine(frame, axisAnchor, &toAvoid);
            }

            computed = !points.empty();

            dbg(
                "[ToolPath] Done. slices=%zu paths=%zu points=%zu axis=%zu duration=%.3fs link=%.3fmm computed=%i",
                strategyImpl.slices().size(),
                strategyImpl.paths().size(),
                points.size(),
                axis.size(),
                points.empty() ? 0.0 : points.back().t,
                hasAxisAnchor && points.size() >= 2
                    ? points[1].position.distanceTo(points[0].position)
                    : 0.0f,
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

        // Linking
        //--------------------------------------------------

        // Connect this path's end (points.back()) to the next path's start (points.front()).
        // Both paths must already be in forward display order from compute().
        bool link(const ToolPath& next) {

            if (points.empty() || next.points.empty()) {
                return false;
            }

            if (linkedPointCount > 0) {
                points.resize(points.size() - linkedPointCount);
                linkedPointCount = 0;
            }

            const Pos3 from = points.back().position;
            const Pos3 to = next.points.front().position;

            addWorldPoint(to, true, false);
            linkedPointCount = 1;

            assignPointTimes();

            dbg(
                "[ToolPath] Linked paths: (%.3f, %.3f, %.3f) -> (%.3f, %.3f, %.3f) distance=%.3fmm",
                from.x, from.y, from.z,
                to.x, to.y, to.z,
                from.distanceTo(to)
            );

            return true;
        }

        // Preview sampling
        //--------------------------------------------------

        // Points are in forward execution order (see reversePointsForForwardDisplay).
        bool sampleAtProgress(double previewProgress, ToolPathPoint& out) const {

            if (points.empty()) { return false; }

            previewProgress = std::clamp(previewProgress, 0.0, 1.0);

            if (points.size() == 1) {
                out = points.front();
                return true;
            }

            const double totalDuration = points.back().t;

            if (totalDuration <= 1e-12) {
                out = points.front();
                return true;
            }

            if (previewProgress <= 0.0) {
                out = points.front();
                return true;
            }

            if (previewProgress >= 1.0 - 1e-12) {
                out = points.back();
                return true;
            }

            const double previewTime = totalDuration * previewProgress;

            for (size_t i = 0; i + 1 < points.size(); i++) {

                const double t0 = points[i].t;
                const double t1 = points[i + 1].t;

                if (previewTime > t1 + 1e-9) { continue; }

                const double span = t1 - t0;
                const float alpha = span > 1e-12
                    ? float((previewTime - t0) / span)
                    : 0.0f;

                const ToolPathPoint& a = points[i];
                const ToolPathPoint& b = points[i + 1];

                out = a;
                out.position = a.position + (b.position - a.position) * alpha;
                out.toolDirection = (a.toolDirection + (b.toolDirection - a.toolDirection) * alpha).normalized();
                out.spindleSpeed = a.spindleSpeed + (b.spindleSpeed - a.spindleSpeed) * double(alpha);
                out.t = previewTime;

                return true;
            }

            out = points.back();
            return true;
        }

        // Rendering
        //--------------------------------------------------

        void buildLineSegments(
            std::vector<Vertex3>& lines,
            double previewProgress = 1.0
        ) const {
            lines.clear();

            if (points.size() < 2) { return; }

            previewProgress = std::clamp(previewProgress, 0.0, 1.0);

            const double totalDuration = points.back().t;
            const double previewTime = totalDuration * previewProgress;

            Color cutColor = { 1.0f, 0.0f, 1.0f, 1.0f };
            Color rapidColor = { 0.6f, 0.0f, 1.0f, 0.35f };
            Color linkColor = { 0.25f, 0.85f, 1.0f, 1.0f };
            Color uncoloredColor = { 0.0f, 0.0f, 0.0f, 0.0f };

            for (size_t i = 0; i + 1 < points.size(); i++) {
                const Pos3& a = points[i].position;
                const Pos3& b = points[i + 1].position;

                const bool isLink = !points[i].cutting || !points[i + 1].cutting;

                Color baseColor = isLink
                    ? linkColor
                    : (points[i + 1].rapid ? rapidColor : cutColor);

                const bool colored = (
                    previewProgress >= 1.0 - 1e-9 ||
                    points[i + 1].t <= previewTime + 1e-9
                );

                Color color = colored ? baseColor : uncoloredColor;

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

        Pos3 slicingToolDirection() const {

            const float len = sliceAxis.pythag();

            if (len <= 1e-6f) {
                return { 0.0f, 0.0f, 1.0f };
            }

            return sliceAxis / len;
        }

        ToolPathPoint makePoint(
            const Pos3& position,
            bool rapid = false,
            bool cutting = true
        ) const {

            return {
                .position = position,
                .toolDirection = slicingToolDirection(),
                .spindleSpeed = 0.0,
                .t = 0.0,
                .rapid = rapid,
                .cutting = cutting
            };
        }

        std::optional<StrategyInstance> strategyInstance;
    };
}
