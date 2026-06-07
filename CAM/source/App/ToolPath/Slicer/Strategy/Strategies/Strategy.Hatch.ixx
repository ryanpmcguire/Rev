module;

#include <vector>
#include <cstddef>
#include <algorithm>

export module Cam.App.Slicer.Strategy.Strategies.Hatch;

import Rev.Core.Pos;

import Cam.App.Model;
import Cam.App.Slicer.Strategy.Strategy;
import Cam.App.Slicer.Strategy.SliceSource;
import Cam.App.Slicer.Strategy.Slice.Slice;
import Cam.App.Slicer.Strategy.Slice.Segment2;
import Cam.App.Slicer.Strategy.Slice.Chain;
import Cam.App.Slicer.Strategy.Slice.Profile;

export namespace Cam::App::Slicer::Strategy::Strategies {

    using namespace Rev::Core;

    using SliceLayer = Slice::Slice;
    using SliceProfile = Slice::Profile;
    using Segment = Slice::Segment;
    using Chain = Slice::Chain;

    struct Hatch : Strategy {

        static constexpr const char* name() { return "Hatch"; }

        static bool detect(const Model& model) {
            return model.loaded;
        }

        // Profiles
        //--------------------------------------------------

        void processSlice(SliceLayer& slice, const StrategyContext& ctx) {

            slice.resetProfiles();
            slice.geometricProfile = SliceProfile(slice.source);

            if (slice.geometricProfile.empty()) {
                return;
            }

            // Same initial boundary as the Profile strategy: outer retreats where
            // it hugs the negative keep-out model and expands into free space;
            // holes (islands) grow outward to protect the kept material.
            std::vector<Segment> keepOut = keepOutSection(ctx, slice.z);

            slice.boundaryProfile =
                slice.geometricProfile.boundaryOffset(toolRadius(ctx), keepOut);

            slice.profiles.push_back(slice.geometricProfile);
            slice.profiles.push_back(slice.boundaryProfile);
        }

        // Section the negative keep-out model at `z`, returning its raw edges in
        // slice (u,v).  Empty when there is no negative model or it misses this
        // height.
        static std::vector<Segment> keepOutSection(const StrategyContext& ctx, float z) {

            if (!ctx.negative) { return {}; }

            SliceLayer negSlice;

            if (!SliceSource::build(*ctx.negative, ctx.frame, z, negSlice)) {
                return {};
            }

            return negSlice.source;
        }

        // Paths
        //--------------------------------------------------

        void buildPaths(const StrategyContext& ctx) {

            paths_.clear();

            const float stepover = stepoverDistance(ctx);

            for (const SliceLayer& slice : slices_) {

                LayerPath layer;
                layer.z = slice.z;

                // Inner roughing: scanline raster fill.
                lawnmower(slice.boundaryProfile, stepover, layer.segments);

                // Always trace the delta-slice boundary itself (outer + holes).
                slice.boundaryProfile.appendSegments(layer.segments, ctx.climbMilling);

                if (layer.segments.empty()) { continue; }

                paths_.push_back(layer);
            }
        }

        // Lawn-mower fill
        //==================================================

        static constexpr size_t NPOS = ~static_cast<size_t>(0);

        // One end of a fill segment.  It always lies on a boundary chain, so we
        // record which chain and where (segment + parameter) so a link can be
        // traced exactly along that chain.
        struct EndRef {
            Pos p;
            size_t chain = NPOS;   // index into boundary.entries
            size_t seg = 0;        // segment index within that chain
            float t = 0.0f;        // parameter within that segment
        };

        // A fill segment: a single scan-ray clipped to inside the region.  `a` is
        // the left end, `b` the right end.  We mow from whichever end we enter to
        // the other, so direction is explicit.
        struct Fill {
            EndRef a;
            EndRef b;
            bool traced = false;
        };

        // Locate a boundary crossing on its chain.
        static EndRef makeEnd(const SliceProfile& boundary, const SliceProfile::XHit& hit, float y) {

            EndRef e;
            e.p = { hit.x, y };
            e.seg = hit.segment;

            if (hit.entry < boundary.entries.size()) {

                const Chain& ch = boundary.entries[hit.entry].chain;

                if (hit.segment < ch.segments.size()) {

                    e.chain = hit.entry;

                    float t = 0.0f;

                    if (Chain::paramOnSegment(ch.segments[hit.segment], e.p, t)) {
                        e.t = std::clamp(t, 0.0f, 1.0f);
                    }
                }
            }

            return e;
        }

        // The Euclidean-nearest untraced endpoint to `from`.  This is the virtual
        // look-ahead: we do NOT move the tool to search, we just find the closest
        // unreached lane end and head for it.  Returns false when none remain.
        static bool nearestEndpoint(const std::vector<Fill>& fills, const Pos& from, size_t& outFill, bool& outIsA) {

            float bestDistance = 1e30f;
            bool found = false;

            for (size_t i = 0; i < fills.size(); i++) {

                if (fills[i].traced) { continue; }

                for (int side = 0; side < 2; side++) {

                    const bool isA = side == 0;
                    const EndRef& e = isA ? fills[i].a : fills[i].b;

                    float d = from.distanceTo(e.p);

                    if (d < bestDistance) {
                        bestDistance = d;
                        outFill = i;
                        outIsA = isA;
                        found = true;
                    }
                }
            }

            return found;
        }

        // Scanline raster fill.
        //
        // 1. Each horizontal scan ray is split by the boundary into inside spans,
        //    exactly like font rasterization: the negative area (outer exterior
        //    and every hole) "cuts" the ray.  A ray crossing one hole becomes two
        //    fills, two holes three, and so on.
        //
        // 2. We then thread the fills together.  After mowing a fill, we look
        //    ahead VIRTUALLY for the Euclidean-nearest unreached endpoint and head
        //    for it — we do not drag the tool around the border to find it.  If
        //    that endpoint sits on the same boundary chain we just left, we link
        //    to it ALONG that chain the shorter way (exact geometry — never
        //    crosses the boundary).  Otherwise we leave a genuine break, which the
        //    point builder turns into a safe retract / rapid / plunge.
        //
        // Cutting passes lie inside the spans and same-chain links lie on the
        // boundary, so no cutting move ever crosses the boundary.
        static void lawnmower(const SliceProfile& boundary, float stepover, std::vector<Segment>& out) {

            if (boundary.empty()) { return; }
            if (stepover <= 0.0f) { return; }

            Pos mn = boundary.min();
            Pos mx = boundary.max();

            if (!mn || !mx) { return; }

            // --- 1. Build fills by splitting scan rays at boundary crossings ---

            std::vector<Fill> fills;

            for (float y = mn.y + stepover * 0.5f; y <= mx.y; y += stepover) {

                std::vector<SliceProfile::XHit> hits;
                boundary.hitsAtY(y, hits);

                const size_t pairCount = hits.size() / 2;

                for (size_t k = 0; k < pairCount; k++) {

                    Fill f;
                    f.a = makeEnd(boundary, hits[k * 2], y);
                    f.b = makeEnd(boundary, hits[k * 2 + 1], y);

                    fills.push_back(f);
                }
            }

            if (fills.empty()) { return; }

            // --- 2. Thread the fills via nearest-endpoint look-ahead ---

            size_t curFill = 0;
            bool curIsA = true;       // enter the first lane at its left end

            // Start at the lowest, then leftmost lane.
            for (size_t i = 1; i < fills.size(); i++) {
                const Pos& p = fills[i].a.p;
                const Pos& q = fills[curFill].a.p;
                if (p.y < q.y || (p.y == q.y && p.x < q.x)) { curFill = i; }
            }

            size_t tracedCount = 0;

            while (tracedCount < fills.size()) {

                Fill& f = fills[curFill];

                EndRef enter = curIsA ? f.a : f.b;
                EndRef leave = curIsA ? f.b : f.a;

                // Mow this fill.
                out.push_back(Segment::Line(enter.p, leave.p));
                f.traced = true;
                tracedCount += 1;

                if (tracedCount >= fills.size()) { break; }

                // Virtual look-ahead to the nearest unreached endpoint.
                size_t nextFill = 0;
                bool nextIsA = true;

                if (!nearestEndpoint(fills, leave.p, nextFill, nextIsA)) { break; }

                const EndRef& target = nextIsA ? fills[nextFill].a : fills[nextFill].b;

                // If it lies on the chain we just left, link along the boundary
                // the shorter way; otherwise leave a break for a safe retract.
                if (
                    leave.chain != NPOS &&
                    leave.chain == target.chain &&
                    leave.chain < boundary.entries.size()
                ) {
                    boundary.entries[leave.chain].chain.traceArc(
                        leave.seg, leave.t, target.seg, target.t, out
                    );
                }

                curFill = nextFill;
                curIsA = nextIsA;
            }
        }
    };
}
