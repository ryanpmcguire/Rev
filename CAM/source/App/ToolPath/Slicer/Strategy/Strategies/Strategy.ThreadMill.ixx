module;

#include <vector>
#include <memory>
#include <cmath>
#include <cstddef>
#include <algorithm>

#include <dbg.hpp>

export module Cam.App.Slicer.Strategy.Strategies.ThreadMill;

import Rev.Core.Pos;
import Rev.Core.Pos3;

import Cam.App.Model;
import Cam.App.Slicer.Strategy.Strategy;
import Cam.App.Slicer.Strategy.Slice.Slice;

export namespace Cam::App::Slicer::Strategy::Strategies {

    using Rev::Core::Pos;
    using Rev::Core::Pos3;

    using SliceLayer = Slice::Slice;

    // Thread milling = a BORE that helixes instead of clearing.  It consumes a
    // profile (a cylinder, or ANY closed profile) exactly like Bore/Profile do:
    // it slices the input and runs the shared slice strategy's BOUNDARY pass to
    // get the tool-centre path one tool radius off the wall.  Then, instead of
    // stepping concentric rings inward, it sweeps that single boundary loop down
    // as a HELIX -- one revolution per thread pitch over the feature depth.  The
    // profile defines the thread's size and shape; the only parameter that makes
    // it a thread rather than a bore is the PITCH.
    struct ThreadMill : Strategy {

        static constexpr const char* name() { return "ThreadMill"; }

        static bool detect(const Model&) { return false; }

        // Condition + boundary pass, identical to the Profile/Bore front end --
        // we only keep generation 1 (the wall-following ring).
        void processSlice(SliceLayer& slice, const StrategyContext& ctx) {

            slice.resetProfiles();

            if (slice.source.empty()) { return; }

            const float radius = static_cast<float>(ctx.tool->radius);

            Geo::Profile seed;
            seed.chains = Geo::Chain::build(slice.source);

            condition(seed);

            Geo::SliceParams params;
            params.kind = Geo::StrategyKind::Profile;
            params.toolRadius = radius;
            params.stepover = 1.0f;
            params.maxGenerations = 1;          // just the boundary: the thread-wall path
            params.reverse = false;
            params.climb = ctx.climbMilling;

            Geo::SliceStrategy strategy(params);
            strategy.ingest(std::move(seed));
            strategy.run();

            slice.result = std::move(strategy.result);
        }

        void buildPaths(const StrategyContext& ctx) {

            paths_.clear();

            if (slices_.empty()) { return; }

            const float pitch = (ctx.threadPitch > 1e-4f) ? ctx.threadPitch : 0.4f;

            // The thread's axial extent, from the slices we kept.
            float zTop = slices_.front().z;
            float zBot = slices_.front().z;
            for (const SliceLayer& s : slices_) {
                zTop = std::max(zTop, s.z);
                zBot = std::min(zBot, s.z);
            }

            const float depth = zTop - zBot;
            if (depth <= 1e-4f) { return; }

            // The cross-section the cutter follows: the boundary ring from the
            // top-most slice that produced one.  (Threads are constant-section,
            // so any slice's boundary is the same loop.)
            const SliceLayer* crossSlice = nullptr;
            for (const SliceLayer& s : slices_) {
                if (s.result.profiles.size() > 1 && !s.result.profiles[1].chains.empty()) {
                    if (!crossSlice || s.z > crossSlice->z) { crossSlice = &s; }
                }
            }

            if (!crossSlice) { return; }

            const Geo::Profile& boundary = crossSlice->result.profiles[1];

            const int turns = std::max(1, static_cast<int>(std::ceil(depth / pitch)));

            // Every closed boundary loop becomes its own helix.  For a single
            // hole that is one helix; "any profile" with several loops threads
            // each.  The loops already carry the correct climb handedness from
            // the boundary pass; up/down only sets the z order.
            for (const Geo::Chain& loop : boundary.chains) {

                if (!loop.closed || loop.edges.empty()) { continue; }

                // One helical LayerPath per revolution.  The point builder
                // consumes layers back-to-front, so to cut BOTTOM-UP we push the
                // top turn first and the bottom turn last.  Each turn reuses the
                // SAME loop (a clone) at the same seam, so consecutive turns join
                // continuously into one unbroken spiral.
                for (int k = turns - 1; k >= 0; k--) {

                    const float zLo = zBot + pitch * float(k);
                    const float zHi = std::min(zTop, zBot + pitch * float(k + 1));

                    LayerPath turn;
                    turn.helical = true;

                    if (ctx.threadUpCut) { turn.z = zLo; turn.zTo = zHi; }   // ramp up
                    else                 { turn.z = zHi; turn.zTo = zLo; }   // ramp down

                    turn.chains.push_back(loop.clone());

                    paths_.push_back(std::move(turn));
                }
            }

            dbg(
                "[ThreadMill] pitch=%.3f depth=%.3f turns=%d loops=%zu (%s)",
                pitch, depth, turns, boundary.chains.size(),
                ctx.threadUpCut ? "up-cut" : "down-cut"
            );
        }
    };
}
