module;

#include <vector>
#include <memory>
#include <cstddef>
#include <cmath>
#include <algorithm>

#include <dbg.hpp>

export module Cam.App.Slicer.Strategy.Strategy;

import Rev.Core.Pos;
import Rev.Core.Pos3;
import Rev.Core.Vertex3;

export import Geo.Strategy;

import Cam.App.Model;
import Cam.App.Tool;

import Cam.App.Slicer.Strategy.CutFrame;
import Cam.App.Slicer.Strategy.Slice.Slice;
import Cam.App.Slicer.Strategy.SliceSource;

export namespace Cam::App::Slicer::Strategy {

    using namespace Rev::Core;

    using SliceLayer = Slice::Slice;

    // One Z layer of tool motion: ordered, link-tagged Geo chains whose
    // sequence and travel direction ARE the tool's motion (`chains`), or a
    // pre-sampled polyline (`points`) for strategies that emit raw moves.
    struct LayerPath {

        float z = 0.0f;
        std::vector<Geo::Chain> chains;
        std::vector<Pos> points;

        // HELICAL layer: depth ramps linearly (by arc length) from `z` at the
        // chain's start to `zTo` at its end, instead of staying flat at `z`.
        // One revolution of a ring chain with a stepdown between z and zTo is
        // one turn of a helix.
        bool helical = false;
        float zTo = 0.0f;
    };

    // Inputs shared by every strategy run.
    struct StrategyContext {

        const Model* positive = nullptr;
        const Model* negative = nullptr;
        const Tool* tool = nullptr;

        float stepDown = 1.0f;
        float stepover = 0.25f;
        bool climbMilling = true;

        // Ring order within a slice: true = INSIDE OUT (cut the innermost ring
        // first, work outward); false = OUTSIDE IN.  Forwarded to the slice
        // strategy's params.reverse, which reverses chain ORDER only (links are
        // re-woven, climb handedness untouched).
        bool insideOut = true;

        // Finishing pass (profile strategy): a thin ring just after the boundary
        // clearance pass. Forwarded to Geo::SliceParams::finishPass / finishWidth.
        bool  finishPass = true;
        float finishWidth = 0.1f;

        // Thread-mill callout + options (only the ThreadMill strategy reads
        // these).  A thread is defined entirely by its callout, so these drive
        // the helix directly.
        float threadMajorDiameter = 2.0f;
        float threadPitch = 0.4f;
        bool  threadInternal = true;
        int   threadPasses = 1;
        bool  threadUpCut = true;

        CutFrame frame = CutFrame::fromAxis({ 0.0f, 0.0f, 1.0f });
    };

    // Base class for slice/profile/path generation.
    struct Strategy {

        Strategy() = default;
        Strategy(Strategy&&) = default;
        Strategy& operator=(Strategy&&) = default;

        virtual ~Strategy() = default;

        // Run
        //--------------------------------------------------

        template<typename S>
        static void run(S& strategy, const StrategyContext& ctx) {

            strategy.slices_.clear();
            strategy.paths_.clear();

            if (!ctx.positive) {
                dbg("[%s] Failed: no positive model", S::name());
                return;
            }

            if (!ctx.tool) {
                dbg("[%s] Failed: no tool", S::name());
                return;
            }

            Pos3 min;
            Pos3 max;

            if (!boundsFromModel(*ctx.positive, min, max)) {
                dbg("[%s] Failed: no positive bounds", S::name());
                return;
            }

            float minDepth = 0.0f;
            float maxDepth = 0.0f;

            ctx.frame.depthRange(min, max, minDepth, maxDepth);

            dbg(
                "[%s] Positive bounds min=(%.3f %.3f %.3f), max=(%.3f %.3f %.3f)",
                S::name(),
                min.x, min.y, min.z,
                max.x, max.y, max.z
            );

            dbg(
                "[%s] Slice axis=(%.3f %.3f %.3f) depth=%.3f..%.3f",
                S::name(),
                ctx.frame.axis.x, ctx.frame.axis.y, ctx.frame.axis.z,
                minDepth,
                maxDepth
            );

            float dz = ctx.stepDown;

            if (dz <= 0.0f) { dz = 1.0f; }

            // Slice depths are computed FROM THE SURFACE: stepdowns are
            // measured from the TOP of the delta volume (in slice-plane
            // depth), stepping down by dz -- and one FINAL slice lands exactly
            // on the bottom-most depth. Never the reverse: aligning steps to
            // the bottom would put the first cut at an arbitrary distance
            // below the surface.
            std::vector<float> depths;

            for (float depth = maxDepth - dz; depth > minDepth + 1e-4f; depth -= dz) {
                depths.push_back(depth);
            }

            depths.push_back(minDepth);

            // The pipeline stores slices bottom-up (display flips to forward).
            std::reverse(depths.begin(), depths.end());

            size_t attempted = 0;

            for (float depth : depths) {
                attempted += 1;

                SliceLayer slice;
                slice.z = depth;

                if (!SliceSource::build(*ctx.positive, ctx.frame, depth, slice)) {
                    dbg("[%s] depth=%.3f: no slice source", S::name(), depth);
                    continue;
                }

                strategy.processSlice(slice, ctx);

                if (!slice.hasProfiles()) {
                    dbg("[%s] depth=%.3f: no profiles", S::name(), depth);
                    continue;
                }

                strategy.slices_.push_back(std::move(slice));
            }

            strategy.buildPaths(ctx);

            dbg(
                "[%s] Done. attempted=%zu slices=%zu paths=%zu",
                S::name(),
                attempted,
                strategy.slices_.size(),
                strategy.paths_.size()
            );
        }

        // Access
        //--------------------------------------------------

        const std::vector<SliceLayer>& slices() const {
            return slices_;
        }

        const std::vector<LayerPath>& paths() const {
            return paths_;
        }

        // Bounds
        //--------------------------------------------------

        static bool boundsFromModel(const Model& model, Pos3& min, Pos3& max) {
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

    protected:

        // Helpers
        //--------------------------------------------------

        static float toolRadius(const StrategyContext& ctx) {
            return static_cast<float>(ctx.tool->radius);
        }

        static float stepoverDistance(const StrategyContext& ctx) {

            float distance = static_cast<float>(ctx.tool->diameter) * ctx.stepover;

            if (distance <= 0.0f) {
                distance = static_cast<float>(ctx.tool->diameter) * 0.25f;
            }

            return distance;
        }

        // Conditioning (OCC section -> doctrine-oriented Geo chains)
        //--------------------------------------------------

        // A section edge's endpoints carry OCC's approximation slop, so a loop
        // can come out of chain-building ALMOST closed. The slicer only ever
        // accepts closed chains -- an almost-loop would silently vanish from
        // the toolpath. Weld it: a gap within tolerance is bridged with a
        // segment and the chain sealed.
        static void weldClosed(Geo::Profile& p, float weldEps = 1e-2f) {

            for (Geo::Chain& c : p.chains) {

                if (c.closed || c.edges.empty()) { continue; }

                Pos a = Geo::Chain::eEnd(*c.edges.back());
                Pos b = Geo::Chain::eStart(*c.edges.front());

                float gap = (b - a).pythag();

                if (gap > weldEps) {
                    dbg("[Strategy] OPEN chain survives conditioning: %zu edges, gap=%.4f", c.edges.size(), gap);
                    continue;
                }

                if (gap > 1e-6f) {
                    c.edges.push_back(std::make_unique<Geo::Segment2>(a, b));
                }

                c.closed = true;
            }
        }

        // Doctrine orientation: a section's winding is arbitrary, so chains are
        // conditioned AT THE DOOR. Containment is MEASURED -- exact winding of
        // every other closed chain around this chain's own extreme point --
        // and the law is absolute: a chain contained by nothing is an OUTER
        // profile and must be true CCW (it bounds the region); a chain
        // contained by anything is an INNER profile and must be true CW (it
        // bounds kept material). After this the engine never reorients
        // anything: handedness IS the meaning.
        static void orientByNesting(Geo::Profile& p) {

            for (Geo::Chain& c : p.chains) {

                if (!c.closed || c.edges.empty()) { continue; }

                Pos probe = Geo::Chain::loopMaxXPoint(c);
                bool contained = false;

                for (const Geo::Chain& other : p.chains) {
                    if (&other == &c || !other.closed) { continue; }
                    if (other.windingAround(probe) != 0) { contained = true; break; }
                }

                int desired = contained ? -1 : 1;
                int sign = c.turningSign();

                if (sign == 0) {
                    dbg("[Strategy] chain with NO measurable turning: %zu edges -- left untouched", c.edges.size());
                    continue;
                }

                if (sign != desired) { c = c.reversed(); }
            }
        }

        // The conditioning gate every strategy passes its seed through before
        // the slicer sees it: weld almost-loops shut, enforce outer-CCW /
        // inner-CW, and report the census so a bad hand-off is VISIBLE.
        static void condition(Geo::Profile& p) {

            weldClosed(p);
            orientByNesting(p);

            size_t ccw = 0, cw = 0, open = 0;

            for (const Geo::Chain& c : p.chains) {
                if (!c.closed) { open += 1; continue; }
                int s = c.turningSign();
                if (s > 0) { ccw += 1; }
                if (s < 0) { cw += 1; }
            }

            dbg("[Strategy] conditioned: %zu chains (%zu CCW outer, %zu CW inner, %zu open)", p.chains.size(), ccw, cw, open);
        }

        // True when an edge lies ALONG the keep-out section (majority of its
        // samples within eps), not merely touching it at a corner.
        static bool edgeCoincident(
            const Geo::Stoicheion& e,
            const std::vector<std::unique_ptr<Geo::Stoicheion>>& keepOut,
            float eps,
            int samples = 8
        ) {
            if (keepOut.empty()) { return false; }

            int near = 0;

            for (int i = 0; i <= samples; i++) {

                Pos p = Geo::Chain::edgePointAt(e, float(i) / float(samples));

                float best = 1e30f;
                for (const auto& o : keepOut) { best = std::min(best, o->distanceTo(p)); }

                if (best <= eps) { near += 1; }
            }

            return near * 2 >= (samples + 1);
        }

        // Mark the free-space edges of region boundaries open-air. A keep-island
        // (CW) hugs keep-material all the way round, so it is never open-air --
        // the engine grows it away from the island naturally.
        static void markOpenAir(
            Geo::Profile& p,
            const std::vector<std::unique_ptr<Geo::Stoicheion>>& keepOut,
            float nearEps = 1e-3f
        ) {
            for (Geo::Chain& c : p.chains) {

                if (!c.closed || c.turningSign() <= 0) { continue; }   // outer (CCW) chains only

                for (auto& e : c.edges) {
                    if (!edgeCoincident(*e, keepOut, nearEps)) { e->openAir = true; }
                }
            }
        }

        // Section the negative keep-out model at `z`, returning its raw edges
        // in slice (u,v).  Empty when there is no negative model or it misses
        // this height.
        static std::vector<std::unique_ptr<Geo::Stoicheion>> keepOutSection(const StrategyContext& ctx, float z) {

            if (!ctx.negative) { return {}; }

            SliceLayer negSlice;

            if (!SliceSource::build(*ctx.negative, ctx.frame, z, negSlice)) {
                return {};
            }

            return std::move(negSlice.source);
        }

        // Output
        std::vector<SliceLayer> slices_;
        std::vector<LayerPath> paths_;
    };
}
