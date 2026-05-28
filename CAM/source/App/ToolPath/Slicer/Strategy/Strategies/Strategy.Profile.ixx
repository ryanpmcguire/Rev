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

            SliceProfile current = slice.geometricProfile;
            float insetAmount = toolRadius(ctx);

            for (int i = 0; i < 24; i++) {

                SliceProfile next = current.inset(insetAmount);

                slice.profiles.push_back(next);

                if (i == 0) {
                    slice.boundaryProfile = next;
                }

                current = next;
                insetAmount = stepoverDistance(ctx);
            }
        }

        // Paths
        //--------------------------------------------------

        void buildPaths(const StrategyContext&) {

            paths_.clear();

            for (const SliceLayer& slice : slices_) {

                LayerPath layer;
                layer.z = slice.z;

                slice.geometricProfile.appendSegments(layer.segments);

                for (const SliceProfile& profile : slice.profiles) {
                    profile.appendSegments(layer.segments);
                }

                if (layer.segments.empty()) { continue; }

                paths_.push_back(layer);
            }
        }
    };
}
