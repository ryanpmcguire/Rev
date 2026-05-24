module;

export module Cam.App.Slicer.Strategy.BoreStrategy;

import Cam.App.Slicer.Strategy.Strategy;
import Cam.App.Slicer.Strategy.StrategyType;
import Cam.App.Slicer.Strategy.Slice.Slice;
import Cam.App.Slicer.Strategy.Slice.Profile;

export namespace Cam::App::Slicer::Strategy {

    using SliceLayer = Slice::Slice;
    using SliceProfile = Slice::Profile;

    struct BoreStrategy : Strategy {

        StrategyType type() const override {
            return StrategyType::Bore;
        }

    protected:

        void processSlice(
            SliceLayer& slice,
            const StrategyContext& ctx
        ) override {

            slice.resetProfiles();
            slice.geometricProfile = Slice::Profile(slice.source);

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

                slice.profiles.push_back(current);
                current = current.inset(stepover);
            }
        }

        void buildPaths(
            const StrategyContext&
        ) override {

            paths_.clear();

            for (const SliceLayer& slice : slices_) {

                LayerPath layer;
                layer.z = slice.z;

                // Skip geometric (0) and boundary (1); path passes follow.
                for (size_t i = 2; i < slice.profiles.size(); i++) {
                    slice.profiles[i].appendSegments(layer.segments);
                }

                if (layer.segments.empty()) { continue; }

                paths_.push_back(layer);
            }
        }
    };
}
