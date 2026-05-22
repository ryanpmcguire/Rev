module;

#include <dbg.hpp>

export module Cam.App.ToolPath.Slicer.HatchSlice;

import Cam.App.Geometry.Profile;

import Cam.App.ToolPath.Slicer.Slice;

export namespace Cam::App::ToolPath::Slicer {

    using namespace Cam::App::Geometry;

    struct HatchSlice : Slice {

        void solve() override {
            buildHatch();
        }

        void buildHatch() {

            points.clear();
            paths.clear();
            profiles.clear();

            Profile profile = makeProfile();

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
