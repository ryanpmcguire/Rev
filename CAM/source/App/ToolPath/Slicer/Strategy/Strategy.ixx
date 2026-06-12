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
    };

    // Inputs shared by every strategy run.
    struct StrategyContext {

        const Model* positive = nullptr;
        const Model* negative = nullptr;
        const Tool* tool = nullptr;

        float stepDown = 1.0f;
        float stepover = 0.25f;
        bool climbMilling = true;

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

            size_t attempted = 0;

            for (float depth = minDepth; depth <= maxDepth + 1e-4f; depth += dz) {
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

        // Doctrine orientation: a mesh section's winding is arbitrary, so chains
        // are conditioned AT THE DOOR -- nesting depth by exact winding around
        // each chain's own extreme point; even depth = region boundary (CCW),
        // odd depth = keep-island (CW). After this, the engine never reorients
        // anything: handedness is the meaning.
        static void orientByNesting(Geo::Profile& p) {

            for (Geo::Chain& c : p.chains) {

                if (!c.closed || c.edges.empty()) { continue; }

                Pos probe = Geo::Chain::loopMaxXPoint(c);
                int depth = 0;

                for (const Geo::Chain& other : p.chains) {
                    if (&other == &c || !other.closed) { continue; }
                    if (other.windingAround(probe) != 0) { depth += 1; }
                }

                int desired = (depth % 2 == 0) ? 1 : -1;
                if (c.turningSign() != desired) { c = c.reversed(); }
            }
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
