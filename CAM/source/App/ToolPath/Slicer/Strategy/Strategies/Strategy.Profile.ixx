module;

export module Cam.App.Slicer.Strategy.Strategies.Profile;

import Cam.App.Model;
import Cam.App.Slicer.Strategy.Strategy;
import Cam.App.Slicer.Strategy.Slice.Slice;
import Cam.App.Slicer.Strategy.Slice.Profile;

export namespace Cam::App::Slicer::Strategy::Strategies {

    using SliceLayer = Slice::Slice;
    using SliceProfile = Slice::Profile;

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

            slice.boundaryProfile =
                slice.geometricProfile.inset(toolRadius(ctx));

            slice.profiles.push_back(slice.geometricProfile);
            slice.profiles.push_back(slice.boundaryProfile);

            SliceProfile current = slice.boundaryProfile;
            float stepover = stepoverDistance(ctx);

            for (int i = 0; i < 24; i++) {

                // Stop bringing the offset in once the chain degenerates
                // (collapsed / self-intersecting), so we never emit garbage.
                if (current.empty() || current.hasDegenerateChain()) { break; }

                slice.profiles.push_back(current);

                SliceProfile next = current.inset(stepover);

                if (next.empty()) { break; }

                // A flipped (negative-area) inversion also ends the offsetting.
                if (SliceProfile::signFlipped(
                        current.signedAreaSum(),
                        next.signedAreaSum()
                )) {
                    break;
                }

                current = next;
            }
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
