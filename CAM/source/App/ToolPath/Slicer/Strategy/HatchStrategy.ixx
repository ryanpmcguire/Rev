module;

export module Cam.App.Slicer.Strategy.HatchStrategy;

import Cam.App.Slicer.Strategy.Strategy;
import Cam.App.Slicer.Strategy.StrategyType;
import Cam.App.Slicer.Strategy.Slice.Slice;
import Cam.App.Slicer.Strategy.Slice.Profile;

export namespace Cam::App::Slicer::Strategy {

    using SliceLayer = Slice::Slice;
    using SliceProfile = Slice::Profile;

    struct HatchStrategy : Strategy {

        StrategyType type() const override {
            return StrategyType::Hatch;
        }

    protected:

        void processSlice(
            SliceLayer& slice,
            const StrategyContext& ctx
        ) override {

            slice.resetProfiles();
            slice.geometricProfile = SliceProfile(slice.source);

            if (slice.geometricProfile.empty()) {
                return;
            }

            slice.boundaryProfile =
                slice.geometricProfile.inset(toolRadius(ctx));

            slice.profiles.push_back(slice.geometricProfile);
            slice.profiles.push_back(slice.boundaryProfile);
        }

        void buildPaths(
            const StrategyContext&
        ) override {

            paths_.clear();

            for (const SliceLayer& slice : slices_) {

                LayerPath layer;
                layer.z = slice.z;

                slice.boundaryProfile.appendSegments(layer.segments);

                if (layer.segments.empty()) { continue; }

                paths_.push_back(layer);
            }
        }
    };
}
