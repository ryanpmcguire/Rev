module;

#include <vector>

export module Cam.App.Slicer.Strategy.Strategies.Profile;

import Cam.App.Model;
import Cam.App.Slicer.Strategy.Strategy;
import Cam.App.Slicer.Strategy.SliceSource;
import Cam.App.Slicer.Strategy.Slice.Slice;
import Cam.App.Slicer.Strategy.Slice.Segment2;
import Cam.App.Slicer.Strategy.Slice.Profile;

export namespace Cam::App::Slicer::Strategy::Strategies {

    using SliceLayer = Slice::Slice;
    using SliceProfile = Slice::Profile;
    using Segment = Slice::Segment;

    struct Profile : Strategy {

        static constexpr const char* name() { return "Profile"; }

        static bool detect(const Model& model) {
            return false;
        }

        // Profiles
        //--------------------------------------------------

        void processSlice(SliceLayer& slice, const StrategyContext& ctx) {

            slice.resetProfiles();
            slice.geometricProfile = SliceProfile(slice.source);

            if (slice.geometricProfile.empty()) {
                return;
            }

            // Section the negative "don't touch" model at this same height so we
            // know which boundary edges hug keep-out material and which face
            // free space.
            std::vector<Segment> keepOut = keepOutSection(ctx, slice.z);

            // Initial boundary: inset where the profile is coincident with the
            // negative model, outset where it faces free space.
            slice.boundaryProfile =
                slice.geometricProfile.boundaryOffset(toolRadius(ctx), keepOut);

            slice.profiles.push_back(slice.geometricProfile);
            slice.profiles.push_back(slice.boundaryProfile);

            appendConcentricInsets(slice, ctx, slice.boundaryProfile);
        }

        // Section the negative keep-out model at `z`, returning its raw edges
        // in slice (u,v).  Empty when there is no negative model or it does not
        // intersect this height.
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

            for (const SliceLayer& slice : slices_) {

                LayerPath layer;
                layer.z = slice.z;

                for (size_t i = 2; i < slice.profiles.size(); i++) {
                    slice.profiles[i].appendSegments(layer.segments, ctx.climbMilling);
                }

                if (layer.segments.empty()) { continue; }

                paths_.push_back(layer);
            }
        }
    };
}
