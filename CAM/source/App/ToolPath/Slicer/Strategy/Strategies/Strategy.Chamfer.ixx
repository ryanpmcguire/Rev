module;

#include <vector>
#include <memory>
#include <cstddef>

#include <dbg.hpp>

export module Cam.App.Slicer.Strategy.Strategies.Chamfer;

import Cam.App.Model;
import Cam.App.Slicer.Strategy.Strategy;
import Cam.App.Slicer.Strategy.Slice.Slice;

export namespace Cam::App::Slicer::Strategy::Strategies {

    using SliceLayer = Slice::Slice;

    // The CHAMFER strategy: a multi-pass stepped contour along a chamfer edge.
    //
    // In the reverse-process model the ChamferOperation has already DEFEATURED the
    // chamfer (removing it, leaving the sharp edge), so the delta volume is the
    // chamfer WEDGE.  The shared slicer cuts that wedge into Z levels by stepdown;
    // at each level we keep the BOUNDARY contour (the gen-1 wall-following ring,
    // exactly like Profile/Bore) and emit it as one pass.  A tall chamfer thus
    // becomes several stepped passes tracking the bevel, top to bottom.
    //
    // The chamfer ANGLE is not a path coordinate -- it selects the cone-shaped
    // chamfer bit (ToolPath::accepts), and that cone, run along these contours,
    // forms the actual bevel.
    struct Chamfer : Strategy {

        static constexpr const char* name() { return "Chamfer"; }

        static bool detect(const Model&) { return false; }

        // Condition + single boundary pass: the wall-following contour at this Z.
        void processSlice(SliceLayer& slice, const StrategyContext& ctx) {

            slice.resetProfiles();

            if (slice.source.empty()) { return; }

            Geo::Profile seed;
            seed.chains = Geo::Chain::build(slice.source);

            condition(seed);

            Geo::SliceParams params;
            params.kind = Geo::StrategyKind::Profile;
            params.toolRadius = toolRadius(ctx);
            params.stepover = 1.0f;
            params.maxGenerations = 1;          // just the boundary contour
            params.reverse = false;
            params.climb = ctx.climbMilling;

            Geo::SliceStrategy strategy(params);
            strategy.ingest(std::move(seed));
            strategy.run();

            slice.result = std::move(strategy.result);
        }

        // One contour pass per kept slice -- the stepped passes down the bevel.
        void buildPaths(const StrategyContext& ctx) {

            paths_.clear();

            size_t passes = 0;

            for (const SliceLayer& s : slices_) {

                if (s.result.profiles.size() < 2) { continue; }

                const Geo::Profile& boundary = s.result.profiles[1];

                for (const Geo::Chain& loop : boundary.chains) {

                    if (loop.edges.empty()) { continue; }

                    LayerPath pass;
                    pass.z = s.z;
                    pass.chains.push_back(loop.clone());

                    paths_.push_back(std::move(pass));
                    passes++;
                }
            }

            dbg("[Chamfer] angle=%.2f passes=%zu (slices=%zu)",
                ctx.chamferAngle, passes, slices_.size());
        }
    };
}
