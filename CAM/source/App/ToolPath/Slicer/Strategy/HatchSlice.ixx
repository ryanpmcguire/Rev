module;

#include <dbg.hpp>

export module Cam.App.Slicer.Strategy.HatchSlice;

import Cam.App.Slicer.Strategy.Slice.Profile;

import Cam.App.Slicer.Strategy.Slice.Slice;

export namespace Cam::App::Slicer::Strategy {

    using SliceLayer = Slice::Slice;
    using SliceProfile = Slice::Profile;

    struct HatchSlice : SliceLayer {

        float toolRadius = 0.0f;
        float stepover = 0.0f;

        void solve() override {
            buildHatch();
        }

        void buildHatch() {

            points.clear();
            paths.clear();
            profiles.clear();

            SliceProfile profile = makeProfile();

            if (profile.empty()) {
                return;
            }

            profiles.push_back(profile);

            SliceProfile toolCenterProfile = profile.inset(toolRadius);

            profiles.push_back(toolCenterProfile);
            collectProfile(toolCenterProfile);

            dbg(
                "[HatchSlice] z=%.3f hatch placeholder source=%zu paths=%zu entries=%zu radius=%.3f stepover=%.3f",
                z,
                source.size(),
                paths.size(),
                profile.size(),
                toolRadius,
                stepover
            );
        }
    };
}
