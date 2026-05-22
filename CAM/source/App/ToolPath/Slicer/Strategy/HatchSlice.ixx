module;

#include <dbg.hpp>

export module Cam.App.Slicer.Strategy.HatchSlice;

import Cam.App.Slicer.Strategy.Slice.Profile;

import Cam.App.Slicer.Strategy.Slice.Slice;

export namespace Cam::App::Slicer::Strategy {

    using SliceLayer = Slice::Slice;
    using SliceProfile = Slice::Profile;

    struct HatchSlice : SliceLayer {

        void solve() override {
            buildHatch();
        }

        void buildHatch() {

            points.clear();
            paths.clear();
            profiles.clear();

            SliceProfile profile = makeProfile();

            if (!profile.empty()) {
                profiles.push_back(profile);
            }

            // Placeholder: keep source visible until hatch is migrated to Profile.
            paths = source;

            dbg(
                "[HatchSlice] z=%.3f hatch placeholder source=%zu paths=%zu entries=%zu",
                z,
                source.size(),
                paths.size(),
                profile.size()
            );
        }
    };
}
