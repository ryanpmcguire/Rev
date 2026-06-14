module;

#include <vector>
#include <memory>
#include <cmath>

export module Cam.App.Slicer.Strategy.Strategies.Profile;

import Rev.Core.Pos;

import Cam.App.Model;
import Cam.App.Slicer.Strategy.Strategy;
import Cam.App.Slicer.Strategy.Slice.Slice;

export namespace Cam::App::Slicer::Strategy::Strategies {

    using Rev::Core::Pos;

    using SliceLayer = Slice::Slice;

    // The PROFILE strategy: a thin CONDITIONING shell around Geo::SliceStrategy
    // -- the very same slice strategy the Sketch app exercises, dropped in with
    // zero modification. This file owns NO toolpathing logic and NO geometry
    // representation of its own: the slice source already IS stoicheia.
    //
    //   1. CONDITION the section into doctrine-oriented chains: CCW bounds the
    //      region to clear, CW bounds keep-islands (nesting parity), and
    //      free-space edges (not hugging the keep-out section) are open-air.
    //
    //   2. RUN Geo::SliceStrategy: boundary pass at exactly the tool radius,
    //      concentric stepover generations, sanity verdicts, depth-first
    //      ordering, climb handedness, entry re-seating, and tagged linking.
    //
    //   3. STORE the result blindly: the slice keeps the strategy's whole
    //      SliceResult -- profiles for the viewers, the final link-tagged
    //      toolpath chains for the 3D layer.
    struct Profile : Strategy {

        static constexpr const char* name() { return "Profile"; }

        static bool detect(const Model& model) {
            return false;
        }

        // Profiles
        //--------------------------------------------------

        void processSlice(SliceLayer& slice, const StrategyContext& ctx) {

            slice.resetProfiles();

            if (slice.source.empty()) { return; }

            // Section the negative "don't touch" model at this same height so
            // we know which boundary edges hug keep-out material and which
            // face free space.
            std::vector<std::unique_ptr<Geo::Stoicheion>> keepOut = keepOutSection(ctx, slice.z);

            const float radius = toolRadius(ctx);

            // 1. CONDITION: section -> doctrine-oriented, open-air-marked chains.
            Geo::Profile seed;
            seed.chains = Geo::Chain::build(slice.source);

            condition(seed);
            markOpenAir(seed, keepOut);

            // 2. RUN the shared slice strategy. Generation 1 clears by exactly
            //    the tool radius; the rings advance by the stepover.
            Geo::SliceParams params;
            params.kind = Geo::StrategyKind::Profile;
            params.toolRadius = radius;
            params.stepover = (radius > 1e-6f) ? stepoverDistance(ctx) / radius : 1.0f;
            params.maxGenerations = 256;
            params.reverse = ctx.insideOut;   // reverse plan = innermost ring first = inside-out
            params.climb = ctx.climbMilling;

            // Lead-in / lead-out: ease onto and off every retract with a ramp. The
            // ramp covers exactly this pass's depth of cut (the stepdown between
            // slices) at a 30 degree plunge -- the CAM app reads back the lead chains
            // and descends / climbs them over that depth.
            params.lead = true;
            params.cuttingDepth = ctx.stepDown;
            params.plungeSlope = 30.0f;

            // Finishing pass: a thin ring just after the boundary clearance pass.
            params.finishPass = ctx.finishPass;
            params.finishWidth = ctx.finishWidth;

            Geo::SliceStrategy strategy(params);
            strategy.ingest(std::move(seed));
            strategy.run();

            // 3. STORE blindly: the whole result rides on the slice.
            slice.result = std::move(strategy.result);
        }

        // Paths
        //--------------------------------------------------

        // The slice strategy already resolved ordering, climb sense, entries
        // and links -- each layer's path IS the slice's toolpath, verbatim.
        void buildPaths(const StrategyContext& ctx) {

            paths_.clear();

            for (const SliceLayer& slice : slices_) {

                if (slice.result.toolpath.empty()) { continue; }

                LayerPath layer;
                layer.z = slice.z;

                for (const Geo::Chain& c : slice.result.toolpath) {
                    layer.chains.push_back(c.clone());
                }

                paths_.push_back(std::move(layer));
            }
        }
    };
}
