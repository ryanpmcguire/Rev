module;

#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <cstddef>
#include <cmath>
#include <algorithm>
#include <memory>

#include <dbg.hpp>

export module Cam.App.ToolPath;

import Rev.Core.Vertex3;
import Rev.Core.Color;
import Rev.Core.Pos;
import Rev.Core.Pos3;

import Cam.App.Model;
import Cam.App.Tool;
import Geo.Strategy;

import Cam.App.Slicer.Strategy.Strategy;
import Cam.App.Slicer.Strategy.CutFrame;
import Cam.App.Slicer.Strategy.Strategies.Bore;
import Cam.App.Slicer.Strategy.Strategies.Profile;
import Cam.App.Slicer.Strategy.Strategies.Hatch;
import Cam.App.Slicer.Strategy.Strategies.ThreadMill;

export namespace Cam::App {

    using namespace Rev::Core;

    struct ToolPathPoint {
        Pos3 position = {};
        Pos3 toolDirection = { 0.0f, 0.0f, 1.0f }; // normalized slice axis at this point
        double spindleSpeed = 0.0; // RPM; placeholder until spindle model exists
        double t = 0.0; // seconds from path start
        bool rapid = false;
        bool cutting = true;

        // A LINKING step: the tool is down and cutting, but the move exists to
        // travel between passes (a Cut-tagged link chain), not to clear stock.
        bool link = false;

        // Per-move feed override (mm/min); 0 = use the toolpath's global feed.
        // Lead ramps and the finishing pass set their own, slower/finer feeds.
        double feedRateOverride = 0.0;
    };

    // Computed tool motion for one material state.
    struct ToolPath {

        using Bore = Slicer::Strategy::Strategies::Bore;
        using Profile = Slicer::Strategy::Strategies::Profile;
        using Hatch = Slicer::Strategy::Strategies::Hatch;
        using ThreadMill = Slicer::Strategy::Strategies::ThreadMill;

        using StrategyInstance = std::variant<Bore, Profile, Hatch, ThreadMill>;

        // Settings
        //
        // toolName is the single tool the operation uses.  (Thread milling once
        // distinguished a separate finish tool, but the thread mill is now JUST
        // the threading pass -- the bore is a prior step -- so one tool suffices.)
        std::string toolName = "";
        // The selected cutting profile (a named OperationProfile owned by the
        // tool).  Empty = use the toolpath's own feed/stepdown/stepover values.
        std::string profileName = "";
        // The profile that drives the strategy's FINISHING pass (Profile strategy).
        // Empty = use the toolpath's own finish* values.
        std::string finishProfileName = "";
        std::string strategy = Hatch::name();
        bool strategyAuto = true;

        double stepDown = 0.5;
        double feedRate = 250.0;
        double stepover = 0.25;
        double rapidSpeedMmPerSec = 10.0;
        bool climbMilling = true;

        // Ring order: true = INSIDE OUT (innermost ring first), false = OUTSIDE
        // IN.  Forwarded to the slice strategy's reverse flag.
        bool insideOut = true;

        // Finishing pass (profile): a thin extra ring taken right after the
        // boundary clearance ring (ladder = 1*R -> finishWidth*R -> stepover),
        // leaving a fine pass.  Forwarded to the slice strategy's finish params.
        bool   finishPass = true;
        double finishWidth = 0.2;     // finishing skin width left on the wall (mm)
        double finishStepdown = 0.25; // final floor stepdown for the global finishing pass (mm)
        double finishFeedRate = 150.0;     // feed for the finishing pass cut (mm/min)
        double finishSpindleSpeed = 12000.0; // spindle for the finishing pass (RPM)

        // Lead-in / lead-out ramp: its slope (degrees off horizontal -> Geo
        // plungeSlope) and its own, gentler feed.
        double leadSlope = 30.0;      // lead-in/out ramp angle (deg)
        double leadFeedRate = 200.0;  // feed for lead-in/out ramps (mm/min)

        // Thread milling callout + options (used only by the ThreadMill
        // strategy).  Seeded from the ThreadMillOperation when the stage is
        // created; edited in the thread-mill settings view.
        double threadMajorDiameter = 2.0;   // nominal thread major diameter (mm)
        double threadPitch = 0.4;           // thread pitch (mm/rev)
        double threadPreBore = 1.6;         // recommended pre-mill bore (mm); mirrors
                                            // the ThreadMillOperation's pre-bore
        bool   threadInternal = true;       // internal (tapped hole) vs external
        int    threadPasses = 1;            // radial passes (1 = single-pass)
        bool   threadUpCut = true;          // true = bottom-up, false = top-down

        // Implied tool REQUIREMENTS the feature imposes
        //--------------------------------------------------
        //
        // DERIVED (recomputed each compute from the delta volume), never edited or
        // serialized.  The counterpart to Tool::Implied: where the tool says what
        // it CAN do, this says what the operation NEEDS.  A tool qualifies only if
        // its diameter fits and its reach covers the feature -- so the selection
        // dropdown can grey out, say, a 5 mm cutter for a 2 mm bore.
        struct Implied {
            double maxToolDiameter  = 0.0;   // widest tool the feature admits (0 = unbounded)
            double minCuttingLength = 0.0;   // depth the tool's flutes/teeth must span
        };
        Implied implied;

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

        // Clearance for every retract / rapid / link: this many mm ABOVE the
        // feature's top surface (the delta volume's highest point along the
        // slice axis). Applies to ALL toolpath strategies.
        float retractHeight = 2.0f;

        // Top of the feature in slice-frame depth, captured at compute().
        float featureTopDepth = 0.0f;

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
            bool cutting = true,
            bool link = false
        ) {
            points.push_back(makePoint(position, rapid, cutting, link));
        }

        void addPoint(
            const Pos& uv,
            float depth,
            const Slicer::Strategy::CutFrame& frame,
            bool rapid = false,
            bool cutting = true,
            bool link = false
        ) {
            addWorldPoint(frame.uvToWorld(uv, depth), rapid, cutting, link);
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

            if (point.cutting && !point.rapid) {
                const double feed = point.feedRateOverride > 0.0 ? point.feedRateOverride : feedRate;
                if (feed > 0.0) { return feed / 60.0; }
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

        // Points are built in true forward execution order, so finalizing is
        // just (re)assigning the time stamps -- no reversal.  (The strategy
        // layers are consumed back-to-front in buildPointsFromStrategy, which
        // is where the bottom-up slice storage is turned into top-down cutting
        // order; intra-chain travel direction is never touched.)
        void finalizePoints() {

            if (points.size() < 2) { return; }

            assignPointTimes();
        }

        // The biggest chord deviation (mm) any tessellated curve may bow away from
        // the true arc. The machine cuts straight moves between sampled points, so
        // this is the actual surface error left on the part -- keep it tight.
        static constexpr float ChordTolerance = 0.01f;

        // Sample one stoicheion in TRAVEL order (edgePointAt walks the edge's
        // own direction, so arcs and exotic curves come out the way the tool
        // actually moves). The sample COUNT is ADAPTIVE: enough chords that the
        // straight cuts between them never bow more than ChordTolerance off the
        // true curve, so a large arc is not faceted into a visible polygon (a fixed
        // count made big radii coarse and small radii wasteful).
        static void sampleEdgeUv(const Geo::Stoicheion& edge, std::vector<Pos>& out) {

            if (edge.type() == Geo::SKind::Segment) {
                out.push_back(Geo::Chain::eStart(edge));
                out.push_back(Geo::Chain::eEnd(edge));
                return;
            }

            int samples = 24;   // fallback

            Pos c; float r = 0.0f, a0 = 0.0f, sweep = 0.0f; int chir = 0;
            if (Geo::Chain::circularOf(edge, c, r, a0, sweep, chir) && r > 1e-4f) {
                // Max angle per chord that holds the sagitta r(1 - cos(dθ/2)) under
                // tolerance: dθ = 2·acos(1 - tol/r). Bigger radius => smaller step.
                const float arg = std::clamp(1.0f - ChordTolerance / r, -1.0f, 1.0f);
                const float dTheta = 2.0f * std::acos(arg);
                if (dTheta > 1e-5f) {
                    samples = std::clamp(
                        static_cast<int>(std::ceil(std::fabs(sweep) / dTheta)), 4, 4096);
                }
            }
            else {
                // Offset ellipse / other: fall back to a fixed chord length.
                const float len = Geo::Chain::edgeLength(edge);
                samples = std::clamp(static_cast<int>(std::ceil(len / 0.2f)), 8, 4096);
            }

            for (int i = 0; i <= samples; i++) {
                out.push_back(Geo::Chain::edgePointAt(edge, float(i) / float(samples)));
            }
        }

        // A whole chain as a travel-ordered polyline.
        static void sampleChainUv(const Geo::Chain& chain, std::vector<Pos>& out) {

            for (const auto& e : chain.edges) {
                sampleEdgeUv(*e, out);
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

            // Per-move feed / spindle for the cut moves emitted next (0 = global);
            // set by the chain loop for lead ramps and the finishing pass.
            double curFeed = 0.0;
            double curSpindle = 0.0;

            auto cutTo = [&](const Pos& uv, float depth, bool link = false) {
                place(uv, depth, false, true);
                points.back().link = link;
                points.back().feedRateOverride = curFeed;
                if (curSpindle > 0.0) { points.back().spindleSpeed = curSpindle; }
            };

            // A cutting RAMP: cut along the polyline while depth interpolates linearly
            // by arc length from zA at the first point to zB at the last. This is how a
            // lead's 2D run becomes a 3D slope -- the descent (lead-in) or climb
            // (lead-out). Executed at cutting feed, never rapid.
            auto rampCut = [&](const std::vector<Pos>& pp, float zA, float zB) {
                if (pp.size() < 2) { return; }
                float total = 0.0f;
                std::vector<float> cum(pp.size(), 0.0f);
                for (size_t i = 1; i < pp.size(); i++) {
                    total += pp[i - 1].distanceTo(pp[i]);
                    cum[i] = total;
                }
                for (size_t i = 1; i < pp.size(); i++) {
                    const float f = (total > 1e-6f) ? cum[i] / total : 1.0f;
                    cutTo(pp[i], zA + (zB - zA) * f);
                }
            };

            std::vector<Pos> pts;

            // The strategy stores its layers in REVERSE execution order (slices
            // bottom-up; bores floor-first).  Consume them back-to-front so the
            // points come out in true forward execution order WITHOUT reversing
            // anything inside a chain -- each chain's travel direction (its
            // climb sense, chosen entry, and link ends) is preserved exactly as
            // the slicer resolved it.
            const std::vector<Slicer::Strategy::LayerPath>& layers = strategyImpl.paths();

            for (size_t li = layers.size(); li-- > 0; ) {

                const Slicer::Strategy::LayerPath& layer = layers[li];

                if (!layer.points.empty()) {

                    moveTo(layer.points.front(), layer.z);

                    for (size_t i = 1; i < layer.points.size(); i++) {
                        cutTo(layer.points[i], layer.z);
                    }

                    continue;
                }

                const float stepZ = static_cast<float>(stepDown);   // this pass's depth of cut

                for (size_t ci = 0; ci < layer.chains.size(); ci++) {

                    const Geo::Chain& chain = layer.chains[ci];

                    pts.clear();
                    sampleChainUv(chain, pts);

                    if (pts.empty()) { continue; }

                    // The cut sits at layer.z; one stepdown shallower is layer.z + stepZ
                    // (depth increases UPWARD, away from the material) -- where a lead
                    // begins / ends its ramp.
                    const float zCut = layer.z;
                    const float zTop = layer.z + stepZ;

                    // Per-move feed / spindle: lead ramps run the gentle lead feed;
                    // the finishing pass's cuts run the finishing feed + spindle;
                    // everything else uses the global feed.
                    const bool isLead = (chain.link == Geo::LinkKind::LeadIn
                                      || chain.link == Geo::LinkKind::LeadOut);
                    if (isLead) {
                        curFeed = leadFeedRate; curSpindle = 0.0;
                    }
                    else if (layer.finishing) {
                        curFeed = finishFeedRate; curSpindle = finishSpindleSpeed;
                    }
                    else {
                        curFeed = 0.0; curSpindle = 0.0;
                    }

                    // A RETRACT link is a tagged traversal, not a cut: get to its end
                    // without cutting via the safe-Z machinery. When it feeds a lead-in,
                    // stop ONE STEPDOWN HIGH so the lead-in does the final descent.
                    if (chain.link == Geo::LinkKind::Retract) {
                        const bool feedsLeadIn = (ci + 1 < layer.chains.size())
                            && (layer.chains[ci + 1].link == Geo::LinkKind::LeadIn);
                        moveTo(pts.back(), feedsLeadIn ? zTop : zCut);
                        continue;
                    }

                    // LEAD-IN: a cutting ramp DOWN from the elevated start onto the cut.
                    // The retract before it already delivered the tool to the start at
                    // zTop (or, for the very first entry, moveTo reaches it now).
                    if (chain.link == Geo::LinkKind::LeadIn) {
                        moveTo(pts.front(), zTop);
                        rampCut(pts, zTop, zCut);
                        continue;
                    }

                    // LEAD-OUT: a cutting ramp UP off the cut, climbing one stepdown so
                    // the following retract lifts from clear air, not the cut wall.
                    if (chain.link == Geo::LinkKind::LeadOut) {
                        moveTo(pts.front(), zCut);
                        rampCut(pts, zCut, zTop);
                        continue;
                    }

                    // Ordinary cuts and CUT links (tool stays down) cut through;
                    // the link tag rides on every point so the viewers can show
                    // linking steps distinctly.
                    const bool isCutLink = (chain.link == Geo::LinkKind::Cut);

                    // A HELICAL layer ramps depth linearly by arc length from
                    // layer.z at the chain's start to layer.zTo at its end.
                    if (layer.helical) {

                        float total = 0.0f;
                        std::vector<float> cum(pts.size(), 0.0f);

                        for (size_t i = 1; i < pts.size(); i++) {
                            total += pts[i - 1].distanceTo(pts[i]);
                            cum[i] = total;
                        }

                        auto depthAt = [&](size_t i) {
                            const float f = (total > 1e-6f) ? cum[i] / total : 1.0f;
                            return layer.z + (layer.zTo - layer.z) * f;
                        };

                        moveTo(pts.front(), layer.z);

                        for (size_t i = 1; i < pts.size(); i++) {
                            cutTo(pts[i], depthAt(i), isCutLink);
                        }

                        continue;
                    }

                    moveTo(pts.front(), layer.z);

                    for (size_t i = 1; i < pts.size(); i++) {
                        cutTo(pts[i], layer.z, isCutLink);
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

                for (const Geo::Chain& chain : layer.chains) {
                    for (const auto& e : chain.edges) {
                        addUv(Geo::Chain::eStart(*e));
                        addUv(Geo::Chain::eEnd(*e));
                    }
                }
            }

            Pos uv = uvCount > 0
                ? Pos { uvSum.x / float(uvCount), uvSum.y / float(uvCount) }
                : Pos {};

            anchorOut = frame.uvToWorld(uv, minDepth);

            return true;
        }

        // The safe clearance plane in slice-frame depth: retractHeight above
        // the FEATURE TOP -- the highest material of the delta volume -- never
        // relative to any anchor layer.
        float safePlaneDepth(
            const Slicer::Strategy::CutFrame& frame,
            const Model* referenceModel
        ) const {

            float safeDepth = featureTopDepth + retractHeight;

            if (
                hasSliceFace() &&
                referenceModel &&
                sliceFaceId < referenceModel->faceCount()
            ) {
                const Pos3 faceRef = referenceModel->facePoint(sliceFaceId);
                const float faceDepth = frame.dotFromOrigin(faceRef);

                safeDepth = std::max(safeDepth, faceDepth + retractHeight);
            }

            return safeDepth;
        }

        Pos3 linkSafePoint(
            const Slicer::Strategy::CutFrame& frame,
            const Pos3& anchor,
            const Model* referenceModel
        ) const {

            const float anchorDepth = frame.dotFromOrigin(anchor);
            const Pos anchorUv = frame.worldToUv(anchor, anchorDepth);

            return frame.uvToWorld(anchorUv, safePlaneDepth(frame, referenceModel));
        }

        // Unit direction AWAY from the source geometry at `p`: the gradient of
        // the source's distance field, by central differences.
        static Pos awayFromWall(
            const std::vector<std::unique_ptr<Geo::Stoicheion>>& source,
            Pos p
        ) {
            auto sdf = [&](Pos q) {
                float best = 1e30f;
                for (const auto& e : source) {
                    if (e) { best = std::min(best, e->distanceTo(q)); }
                }
                return best;
            };

            const float h = 1e-2f;

            Pos g = {
                (sdf({ p.x + h, p.y }) - sdf({ p.x - h, p.y })) / (2.0f * h),
                (sdf({ p.x, p.y + h }) - sdf({ p.x, p.y - h })) / (2.0f * h)
            };

            const float len = g.pythag();

            if (len <= 1e-6f) { return {}; }

            return g / len;
        }

        // The operation's entry and exit are VERTICAL. The first point of the
        // toolpath sits DIRECTLY ABOVE the first point of interest (never a
        // diagonal from some anchor); the final point sits directly above the
        // tool's last position -- which, for the PROFILE strategy, is first
        // stepped slightly away from the wall (along the gradient of the last
        // slice's source distance field) so the retract never drags up the
        // finished surface.
        void addApproachRetractLinks(
            const Slicer::Strategy::CutFrame& frame,
            const Slicer::Strategy::Strategy& strategyImpl,
            float safeDepth
        ) {

            if (points.empty()) { return; }

            // Points are in true forward execution order: FRONT executes first,
            // BACK executes last.

            // Approach: arrive on the safe plane directly above the first cut
            // point, then plunge straight in.  Prepended so it runs first.
            {
                const Pos3 first = points.front().position;
                const Pos uv = frame.worldToUv(first, frame.dotFromOrigin(first));

                points.insert(
                    points.begin(),
                    makePoint(frame.uvToWorld(uv, safeDepth), true, false)
                );
            }

            // Retract: at the last cut point, step slightly away from the wall
            // at depth (profile only, along the source distance-field gradient
            // so the lift never drags up the finished surface), then lift
            // straight up to the safe plane.  Appended so it runs last.
            {
                const Pos3 last = points.back().position;
                const float lastDepth = frame.dotFromOrigin(last);

                Pos uv = frame.worldToUv(last, lastDepth);

                if (strategy == Profile::name()) {

                    const auto& slices = strategyImpl.slices();

                    // The last executed slice is the bottom-most (stored first).
                    if (!slices.empty()) {

                        const Pos away = awayFromWall(slices.front().source, uv);
                        const float nudge = static_cast<float>(toolDiameter) * 0.25f;

                        if (away.pythag() > 0.5f && nudge > 0.0f) {
                            uv += away * nudge;
                            addWorldPoint(frame.uvToWorld(uv, lastDepth), false, false);
                        }
                    }
                }

                addWorldPoint(frame.uvToWorld(uv, safeDepth), true, false);
            }
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

        // Derive the feature's tool REQUIREMENTS from the STRATEGY's own slices.
        // minCuttingLength is the cut depth (surface to the deepest slice).
        // maxToolDiameter is found by ASKING THE SLICER: a tool fits only while
        // insetting the section by its radius preserves the section's topology --
        // the moment a chain vanishes or the closed-chain count changes, the tool
        // is too large.  (This is exactly generation 1's offset, so it agrees with
        // what the cut would actually do, and it handles an annulus correctly: the
        // ring pinches shut at half its width, not at the outer diameter.)
        void computeImplied(const Slicer::Strategy::Strategy& strategyImpl) {

            implied = Implied{};

            const auto& slices = strategyImpl.slices();
            if (slices.empty()) { return; }

            auto countClosed = [](const auto& prof) {
                size_t n = 0;
                for (const auto& c : prof.chains) { if (c.closed) { n++; } }
                return n;
            };

            // A loose upper bound on the inset radius: half the section's narrowest
            // bounding span -- past this the inset has certainly collapsed.
            auto bboxHalfSpan = [](const auto& prof) -> float {
                bool first = true;
                float uMin = 0.0f, uMax = 0.0f, vMin = 0.0f, vMax = 0.0f;
                for (const auto& c : prof.chains) {
                    for (const auto& e : c.edges) {
                        for (int k = 0; k <= 8; k++) {
                            const Pos q = Geo::Chain::edgePointAt(*e, float(k) / 8.0f);
                            if (first) { uMin = uMax = q.x; vMin = vMax = q.y; first = false; }
                            else {
                                uMin = std::min(uMin, q.x); uMax = std::max(uMax, q.x);
                                vMin = std::min(vMin, q.y); vMax = std::max(vMax, q.y);
                            }
                        }
                    }
                }
                if (first) { return 0.0f; }
                return 0.5f * std::min(uMax - uMin, vMax - vMin);
            };

            // Largest radius whose inset still has the SAME closed-chain count as
            // the ancestor -- i.e. leaves the topology untouched.  A valid tool
            // neither collapses a loop (too big for a bore) NOR mitoses one (splits
            // a profile across a narrow neck).  Both show up as a changed chain
            // count, and either can happen at an INTERMEDIATE radius, so we scan
            // forward and stop at the FIRST change (a binary search would miss a
            // mid-range split when both endpoints happen to match the ancestor).
            auto maxFittingRadius = [&](const auto& seed) -> float {
                const size_t n0 = countClosed(seed);
                if (n0 == 0) { return 0.0f; }
                const float hi = bboxHalfSpan(seed);
                if (hi <= 1e-4f) { return 0.0f; }

                const int steps = 24;
                float good = 0.0f;   // largest scanned radius leaving topology intact
                float bad  = hi;
                bool  broke = false;

                for (int i = 1; i <= steps; i++) {
                    const float r = hi * float(i) / float(steps);
                    if (countClosed(seed.offsetBy(r)) == n0) { good = r; }
                    else { bad = r; broke = true; break; }   // first topology change
                }

                if (!broke) { return good; }   // never changed within the bound

                // Refine the [good, bad] threshold.
                for (int it = 0; it < 8; it++) {
                    const float mid = 0.5f * (good + bad);
                    if (countClosed(seed.offsetBy(mid)) == n0) { good = mid; }
                    else { bad = mid; }
                }
                return good;
            };

            // Depth from the deepest cut; the tool-fit probe needs only ONE
            // section -- the deepest slice (smallest z), the most constraining and
            // the one a tool must actually reach.  (Z grows upward away from the
            // material, so the deepest cut is the minimum z.)
            bool   haveZ = false;
            float  minZ = 0.0f;
            size_t deepestIdx = slices.size();

            for (size_t i = 0; i < slices.size(); i++) {

                const float z = slices[i].z;
                if (!haveZ || z < minZ) { minZ = z; haveZ = true; }

                if (!slices[i].result.profiles.empty()) {
                    if (deepestIdx == slices.size() || z < slices[deepestIdx].z) {
                        deepestIdx = i;
                    }
                }
            }

            implied.minCuttingLength = std::max(0.0f, featureTopDepth - minZ);

            // A single offset-topology probe on the deepest section.
            if (deepestIdx < slices.size()) {
                const float r = maxFittingRadius(slices[deepestIdx].result.profiles[0]);
                if (r > 0.0f) { implied.maxToolDiameter = 2.0f * r; }
            }
        }

        // Whether `tool` can actually perform THIS operation -- the single
        // authority shared by the selection dropdown (grey-out) and the auto-tool
        // picker.  Combines the tool's own capability envelope with the feature's
        // requirements (computed into `implied` at the last compute).
        bool accepts(const Tool& tool) const {

            // Must be able to cut at all (a probe cannot).
            if (!tool.implied.canCut) { return false; }

            // Thread milling: needs a thread mill whose pitch range covers the
            // callout and whose crest fits inside the thread's major diameter.
            if (strategy == ThreadMill::name()) {
                if (!tool.implied.canMillThreads) { return false; }
                if (threadPitch > 0.0 && !tool.canCutThreadPitch(threadPitch)) { return false; }
                if (threadMajorDiameter > 0.0 && tool.diameter >= threadMajorDiameter) { return false; }
            }

            // Feature fit (all strategies): the tool must pass through the
            // narrowest section and reach the full depth.
            if (implied.maxToolDiameter > 1e-6 && tool.diameter > implied.maxToolDiameter + 1e-6) {
                return false;
            }
            if (implied.minCuttingLength > 1e-6 && tool.implied.maxCutDepth < implied.minCuttingLength - 1e-6) {
                return false;
            }

            return true;
        }

        // Adopt the selected cutting profile's speeds/feeds from the tool (no-op
        // when none is selected or the named profile is absent).
        void applyProfile(const Tool& tool) {
            const OperationProfile* p = tool.findProfile(profileName);
            if (!p) { return; }
            feedRate = p->feedRate;
            stepDown = p->stepdown;
            stepover = p->stepover;
            rapidSpeedMmPerSec = p->rapidSpeed;
            climbMilling = p->climbMilling;
            // The plunge rate drives the gentle lead-in/out (Z-entry) ramp feed.
            // (spindleSpeed is carried on the profile but not yet consumed -- the
            // toolpath has no spindle model yet.)
            leadFeedRate = p->plungeRate;
        }

        // Adopt the selected FINISHING profile's speeds/feeds for the strategy's
        // finishing pass (no-op when none is selected).  An atomic profile drives
        // ONE engagement; the toolpath references a roughing profile AND a
        // finishing profile, mapping onto the rough / finish passes respectively.
        void applyFinishProfile(const Tool& tool) {
            const OperationProfile* p = tool.findProfile(finishProfileName);
            if (!p) { return; }
            finishFeedRate = p->feedRate;
            finishStepdown = p->stepdown;
            finishSpindleSpeed = p->spindleSpeed;
        }

        bool compute(Model& toCarve, Model& toAvoid, const Tool& tool) {
            clearPathData();

            toolName = tool.name;
            toolDiameter = tool.diameter;
            toolLength = tool.totalLength();

            // Selected cutting profiles drive the speeds/feeds: one for the
            // roughing pass, one for the finishing pass.
            applyProfile(tool);
            applyFinishProfile(tool);

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

            // The feature's top surface in slice-frame depth: the reference
            // every retract measures from.
            featureTopDepth = 0.0f;
            {
                Pos3 bMin, bMax;

                if (Slicer::Strategy::Strategy::boundsFromModel(toCarve, bMin, bMax)) {
                    float lo = 0.0f, hi = 0.0f;
                    frame.depthRange(bMin, bMax, lo, hi);
                    featureTopDepth = hi;
                }
            }

            Slicer::Strategy::StrategyContext ctx {
                .positive = &toCarve,
                .negative = &toAvoid,
                .tool = &tool,
                .stepDown = static_cast<float>(stepDown),
                .stepover = static_cast<float>(stepover),
                .climbMilling = climbMilling,
                .insideOut = insideOut,
                .finishPass = finishPass,
                .finishWidth = static_cast<float>(finishWidth),
                .finishStepdown = static_cast<float>(finishStepdown),
                .leadSlope = static_cast<float>(leadSlope),
                .threadMajorDiameter = static_cast<float>(threadMajorDiameter),
                .threadPitch = static_cast<float>(threadPitch),
                .threadInternal = threadInternal,
                .threadPasses = threadPasses,
                .threadUpCut = threadUpCut,
                .frame = frame
            };

            if (strategy == Bore::name()) { strategyInstance = Bore {}; }
            else if (strategy == Profile::name()) { strategyInstance = Profile {}; }
            else if (strategy == ThreadMill::name()) { strategyInstance = ThreadMill {}; }
            else {
                strategy = Hatch::name();
                strategyInstance = Hatch {};
            }

            std::visit([&ctx](auto& s) {
                Slicer::Strategy::Strategy::run(s, ctx);
            }, *strategyInstance);

            const Slicer::Strategy::Strategy& strategyImpl =
                *strategyResult();

            // The feature's tool requirements, read from the slices the strategy
            // just produced (drives the selection dropdown).
            computeImplied(strategyImpl);

            // Safe plane for every retract: the same clearance height the
            // approach/exit use.
            const float safeDepth = safePlaneDepth(frame, &toAvoid);

            buildPointsFromStrategy(strategyImpl, frame, safeDepth);

            if (!points.empty()) {
                addApproachRetractLinks(frame, strategyImpl, safeDepth);
            }

            finalizePoints();

            Pos3 axisAnchor = {};

            if (computeAxisAnchor(strategyImpl, frame, axisAnchor)) {
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
                points.size() >= 2
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
        bool link(const ToolPath& next, const Pos3& pivot, const Pos3& rotaryAxis, float safeRadius = 0.0f) {
            return linkPoints(next, &pivot, &rotaryAxis, safeRadius);
        }

    private:

        bool linkPoints(
            const ToolPath& next,
            const Pos3* pivot,
            const Pos3* rotaryAxis,
            float safeRadius = 0.0f
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
                    const float angle    = totalAngle * alpha;

                    // Base radius lerps between the two endpoints, but the part
                    // sweeps between them: a wide prism's CORNERS reach much
                    // farther from the rotary axis than its faces, so an arc at
                    // face radius clips through.  Bulge the swept radius out to
                    // safeRadius (the part's clearance radius about the axis)
                    // across the interior of the sweep, easing smoothly back to
                    // the exact endpoints so the motion stays elegant -- never
                    // a sudden radial jump.
                    float radius = fromRadius + (toRadius - fromRadius) * alpha;

                    if (safeRadius > radius) {
                        constexpr float ease = 0.15f;   // ramp fraction at each end
                        float w = 1.0f;
                        if (alpha < ease)            { w = alpha / ease; }
                        else if (alpha > 1.0f - ease){ w = (1.0f - alpha) / ease; }
                        w = w * w * (3.0f - 2.0f * w);   // smoothstep
                        radius += (safeRadius - radius) * w;
                    }

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
                " angle=%.1f deg dist=%.1fmm",
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

        // Points are in forward execution order (see finalizePoints).
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

            Color cutColor = { 1.0f, 0.0f, 1.0f, 1.0f };           // cutting: magenta
            Color linkColor = { 0.25f, 0.85f, 1.0f, 1.0f };        // retract/rapid links: cyan
            Color cutLinkColor = { 1.0f, 0.58f, 0.15f, 1.0f };     // tool-down linking steps: orange
            Color uncoloredColor = { 0.0f, 0.0f, 0.0f, 0.0f };

            for (size_t i = 0; i + 1 < points.size(); i++) {
                const Pos3& a = points[i].position;
                const Pos3& b = points[i + 1].position;

                // Each point is tagged (by cutTo/moveTo) with the move that
                // ARRIVES at it.  Points are now in true forward execution
                // order (no list-wide reversal), so a segment's identity is its
                // DESTINATION's tag.
                const ToolPathPoint& dst = points[i + 1];

                Color baseColor =
                    !dst.cutting ? linkColor       // a reposition: retract / rapid / plunge / approach
                  :  dst.link    ? cutLinkColor    // a tool-down LINK between passes
                  :                cutColor;        // a genuine cutting move

                // A segment is "reached" once the tool arrives at its END.
                const bool colored = (
                    previewProgress >= 1.0 - 1e-9 ||
                    dst.t <= previewTime + 1e-9
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
            bool cutting = true,
            bool link = false
        ) const {

            return {
                .position = position,
                .toolDirection = slicingToolDirection(),
                .spindleSpeed = 0.0,
                .t = 0.0,
                .rapid = rapid,
                .cutting = cutting,
                .link = link
            };
        }

        std::optional<StrategyInstance> strategyInstance;
    };
}
