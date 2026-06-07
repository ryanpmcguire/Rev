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

        double stepDown = 0.5;
        double feedRate = 250.0;
        double stepover = 0.25;
        double rapidSpeedMmPerSec = 10.0;
        bool climbMilling = true;

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

        float linkRetractDistance = 10.0f;

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

        double durationSeconds() const {

            if (points.empty()) { return 0.0; }

            return points.back().t;
        }

        // Carvera Air rotary (A) axis slew rate.  The real machine rotates the
        // 4th axis at only ~10-20 deg/s, so any move that swings the tool
        // direction (i.e. cross-setup links) is paced by this, not by the much
        // faster linear rapid speed.  Midpoint of the observed range.
        static constexpr double RotarySpeedDegPerSec = 15.0;

        double speedMmPerSecForPoint(const ToolPathPoint& point) const {

            if (point.cutting && !point.rapid && feedRate > 0.0) {
                return feedRate / 60.0;
            }

            return rapidSpeedMmPerSec > 0.0 ? rapidSpeedMmPerSec : 10.0;
        }

        // Time the rotary axis needs to swing the tool direction from a -> b.
        // Tool direction only changes between setups, so this is ~0 within a
        // single operation and only matters across links.
        static double rotaryTimeBetween(const Pos3& a, const Pos3& b) {

            const float la = a.pythag();
            const float lb = b.pythag();

            if (la <= 1e-6f || lb <= 1e-6f) { return 0.0; }

            float cosA = a.dot(b) / (la * lb);
            cosA = std::clamp(cosA, -1.0f, 1.0f);

            const double degrees = std::acos(double(cosA)) * 57.29577951308232;

            if (RotarySpeedDegPerSec <= 0.0) { return 0.0; }

            return degrees / RotarySpeedDegPerSec;
        }

        void assignPointTimes() {

            if (points.empty()) { return; }

            points.front().t = 0.0;

            for (size_t i = 1; i < points.size(); i++) {

                const float distance =
                    points[i - 1].position.distanceTo(points[i].position);

                const double speed = speedMmPerSecForPoint(points[i]);

                const double linearTime = speed > 0.0
                    ? double(distance) / speed
                    : 0.0;

                // A coordinated linear + rotary move takes as long as its
                // slowest component; rotary dominates the cross-setup links.
                const double rotaryTime = rotaryTimeBetween(
                    points[i - 1].toolDirection,
                    points[i].toolDirection
                );

                points[i].t = points[i - 1].t + std::max(linearTime, rotaryTime);
            }
        }

        // Generation appends in reverse execution order; flip for display and preview.
        void reversePointsForForwardDisplay() {

            if (points.size() < 2) { return; }

            std::reverse(points.begin(), points.end());
            assignPointTimes();
        }

        static void sampleSegmentUv(const Segment& segment, std::vector<Pos>& out, int samples = 24) {

            if (segment.kind == Segment::Kind::Line) {
                out.push_back(segment.start());
                out.push_back(segment.end());
                return;
            }

            for (int i = 0; i <= samples; i++) {
                out.push_back(segment.at(float(i) / float(samples)));
            }
        }

        // Build motion points from the strategy's paths.  Consecutive segments
        // that already meet are cut straight through — the end of one path IS the
        // start of the next.  Wherever they do NOT meet (a new lane, a different
        // boundary chain, the next slice) we never glide across the part: we
        // retract along the tool axis to the safe plane, rapid across, then plunge
        // back down.  So every transition is an explicit, collision-free motion.
        void buildPointsFromStrategy(
            const Slicer::Strategy::Strategy& strategyImpl,
            const Slicer::Strategy::CutFrame& frame,
            float safeDepth
        ) {
            points.clear();

            constexpr float eps = 1e-3f;

            bool have = false;
            Pos curUv = {};
            float curDepth = 0.0f;

            auto place = [&](const Pos& uv, float depth, bool rapid, bool cutting) {
                addPoint(uv, depth, frame, rapid, cutting);
                curUv = uv;
                curDepth = depth;
                have = true;
            };

            // Reach (uv, depth) without cutting through anything: continue if we
            // are already there, otherwise retract / rapid / plunge via safe Z.
            auto moveTo = [&](const Pos& uv, float depth) {

                if (!have) {
                    place(uv, depth, false, false);
                    return;
                }

                if (curUv.distanceTo(uv) <= eps && std::abs(curDepth - depth) <= eps) {
                    return;
                }

                place(curUv, safeDepth, true, false);   // retract straight up
                place(uv, safeDepth, true, false);       // rapid across at safe Z
                place(uv, depth, true, false);           // plunge to the start
            };

            auto cutTo = [&](const Pos& uv, float depth) {
                place(uv, depth, false, true);
            };

            std::vector<Pos> pts;

            for (const Slicer::Strategy::LayerPath& layer : strategyImpl.paths()) {

                if (!layer.points.empty()) {

                    moveTo(layer.points.front(), layer.z);

                    for (size_t i = 1; i < layer.points.size(); i++) {
                        cutTo(layer.points[i], layer.z);
                    }

                    continue;
                }

                for (const Segment& segment : layer.segments) {

                    pts.clear();
                    sampleSegmentUv(segment, pts);

                    if (pts.empty()) { continue; }

                    moveTo(pts.front(), layer.z);

                    for (size_t i = 1; i < pts.size(); i++) {
                        cutTo(pts[i], layer.z);
                    }
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
            toolLength = tool.totalLength();

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
                .climbMilling = climbMilling,
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

            Pos3 axisAnchor = {};
            const bool hasAxisAnchor = computeAxisAnchor(strategyImpl, frame, axisAnchor);

            // Safe plane for in-path retracts: the same clearance height the
            // approach/retract links use.
            float safeDepth = 0.0f;

            if (hasAxisAnchor) {
                const Pos3 safeWorld = linkSafePoint(frame, axisAnchor, &toAvoid);
                safeDepth = frame.dotFromOrigin(safeWorld);
            }

            buildPointsFromStrategy(strategyImpl, frame, safeDepth);

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

        // Number of interpolated points generated for each cross-state link.
        static constexpr int LinkSteps = 32;

        // Straight-line link (fallback when no rotary axis is known).
        // Interpolates both position and tool direction linearly.
        bool link(const ToolPath& next) {
            return linkPoints(next, nullptr, nullptr);
        }

        // Arc link — generates points that sweep around the rotary axis
        // in polar coordinates, rather than cutting straight through space.
        //
        // The arc is computed as follows:
        //   1. Both endpoints are decomposed into (axial, radius, angle)
        //      relative to the pivot + rotaryAxis frame.
        //   2. The angle is swept linearly from 0 → totalAngle using atan2
        //      to find the short-path direction.  Radius and axial component
        //      are lerped independently.
        //   3. World positions are reconstructed: pivot + A*axial + r*(cos·u + sin·v)
        //      where (u, v) form an orthonormal basis in the rotation plane.
        //   4. Tool direction is lerped + renormalised at each step.
        //
        // Falls back to straight-line when the endpoints are on the axis or
        // the tool directions are already parallel (no rotation needed).
        bool link(const ToolPath& next, const Pos3& pivot, const Pos3& rotaryAxis) {
            return linkPoints(next, &pivot, &rotaryAxis);
        }

    private:

        bool linkPoints(
            const ToolPath& next,
            const Pos3* pivot,
            const Pos3* rotaryAxis
        ) {
            if (points.empty() || next.points.empty()) { return false; }

            if (linkedPointCount > 0) {
                points.resize(points.size() - linkedPointCount);
                linkedPointCount = 0;
            }

            const ToolPathPoint& fromPt = points.back();
            const ToolPathPoint& toPt   = next.points.front();

            // ── Try to set up the polar arc ──────────────────────────────
            bool  useArc     = false;
            Pos3  A          = {};
            Pos3  u          = {};
            Pos3  v          = {};
            float fromAxial  = 0.0f, toAxial  = 0.0f;
            float fromRadius = 0.0f, toRadius = 0.0f;
            float totalAngle = 0.0f;

            if (pivot && rotaryAxis) {

                A = rotaryAxis->normalized();

                const Pos3 fromRel   = fromPt.position - *pivot;
                fromAxial            = fromRel.dot(A);
                const Pos3 fromRadial = fromRel - A * fromAxial;
                fromRadius           = fromRadial.pythag();

                const Pos3 toRel     = toPt.position - *pivot;
                toAxial              = toRel.dot(A);
                const Pos3 toRadial  = toRel - A * toAxial;
                toRadius             = toRadial.pythag();

                const bool hasRotation =
                    (fromPt.toolDirection - toPt.toolDirection).pythag() > 1e-4f;

                if (fromRadius > 1e-4f && toRadius > 1e-4f && hasRotation) {

                    // Orthonormal basis in the rotation plane.
                    u = fromRadial / fromRadius;         // radial direction at fromPt
                    v = A.cross(u).normalized();          // completes the right-hand frame

                    // Project toRadial onto (u, v) to find the sweep angle.
                    // atan2 naturally gives the short-path angle in (-π, π].
                    const float cosA = toRadial.dot(u) / toRadius;
                    const float sinA = toRadial.dot(v) / toRadius;
                    totalAngle = std::atan2(sinA, cosA);

                    useArc = true;
                }
            }

            // ── Generate interpolated link points ─────────────────────────
            for (int i = 1; i <= LinkSteps; i++) {

                const float alpha = float(i) / float(LinkSteps);

                Pos3 pos;
                Pos3 dir;

                if (useArc) {
                    const float axial    = fromAxial  + (toAxial  - fromAxial)  * alpha;
                    const float radius   = fromRadius + (toRadius - fromRadius) * alpha;
                    const float angle    = totalAngle * alpha;
                    const Pos3 radialDir = u * std::cos(angle) + v * std::sin(angle);
                    pos = *pivot + A * axial + radialDir * radius;

                    // Direction: the OUTWARD unit radial (away from the rotary
                    // axis).  toolDirection points up the tool axis, away from
                    // the part — i.e. outward, not toward the pivot.  This is a
                    // pure rotation of u about A, so it is smooth and never
                    // passes through zero (antipodal-safe), and it matches the
                    // cut directions at both arc endpoints.
                    dir = radialDir;
                }
                else {
                    pos = fromPt.position + (toPt.position - fromPt.position) * alpha;

                    // Lerp + renormalize is fine here since we only reach this
                    // branch when directions are already parallel (no rotation).
                    dir = (
                        fromPt.toolDirection * (1.0f - alpha) +
                        toPt.toolDirection   * alpha
                    ).normalized();
                }

                ToolPathPoint pt;
                pt.position      = pos;
                pt.toolDirection = dir;
                pt.rapid         = true;
                pt.cutting       = false;
                pt.spindleSpeed  = 0.0;
                pt.t             = 0.0;   // assigned below

                points.push_back(pt);
            }

            linkedPointCount = LinkSteps;
            assignPointTimes();

            dbg(
                "[ToolPath] Link (%s) %d steps:"
                " (%.1f,%.1f,%.1f)->(%.1f,%.1f,%.1f)"
                " angle=%.1f° dist=%.1fmm",
                useArc ? "arc" : "line",
                LinkSteps,
                fromPt.position.x, fromPt.position.y, fromPt.position.z,
                toPt.position.x,   toPt.position.y,   toPt.position.z,
                useArc ? double(totalAngle) * 57.2958 : 0.0,
                fromPt.position.distanceTo(toPt.position)
            );

            return true;
        }

    public:

        // Preview sampling
        //--------------------------------------------------

        // Points are in forward execution order (see reversePointsForForwardDisplay).
        bool sampleAtTime(double previewTimeSeconds, ToolPathPoint& out) const {

            if (points.empty()) { return false; }

            if (points.size() == 1) {
                out = points.front();
                return true;
            }

            const double totalDuration = durationSeconds();

            previewTimeSeconds = std::clamp(previewTimeSeconds, 0.0, totalDuration);

            if (totalDuration <= 1e-12) {
                out = points.front();
                return true;
            }

            if (previewTimeSeconds <= 0.0) {
                out = points.front();
                return true;
            }

            if (previewTimeSeconds >= totalDuration - 1e-12) {
                out = points.back();
                return true;
            }

            for (size_t i = 0; i + 1 < points.size(); i++) {

                const double t0 = points[i].t;
                const double t1 = points[i + 1].t;

                if (previewTimeSeconds > t1 + 1e-9) { continue; }

                const double span = t1 - t0;
                const float alpha = span > 1e-12
                    ? float((previewTimeSeconds - t0) / span)
                    : 0.0f;

                const ToolPathPoint& a = points[i];
                const ToolPathPoint& b = points[i + 1];

                out = a;
                out.position = a.position + (b.position - a.position) * alpha;
                out.toolDirection = (a.toolDirection + (b.toolDirection - a.toolDirection) * alpha).normalized();
                out.spindleSpeed = a.spindleSpeed + (b.spindleSpeed - a.spindleSpeed) * double(alpha);
                out.t = previewTimeSeconds;

                return true;
            }

            out = points.back();
            return true;
        }

        bool sampleAtProgress(double previewProgress, ToolPathPoint& out) const {

            const double totalDuration = durationSeconds();

            return sampleAtTime(totalDuration * std::clamp(previewProgress, 0.0, 1.0), out);
        }

        // Rendering
        //--------------------------------------------------

        void buildLineSegments(
            std::vector<Vertex3>& lines,
            double previewProgress = 1.0
        ) const {
            lines.clear();

            if (points.size() < 2) { return; }

            const double totalDuration = durationSeconds();
            const double previewTime = totalDuration * std::clamp(previewProgress, 0.0, 1.0);

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
