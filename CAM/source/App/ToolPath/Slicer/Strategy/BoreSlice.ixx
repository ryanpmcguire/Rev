module;

#include <vector>

#include <dbg.hpp>

export module Cam.App.Slicer.Strategy.BoreSlice;

import Cam.App.Slicer.Strategy.Slice.Profile;
import Cam.App.Slicer.Strategy.Slice.Slice;

export namespace Cam::App::Slicer::Strategy {

    using SliceLayer = Slice::Slice;
    using SliceProfile = Slice::Profile;

    struct BoreSlice : SliceLayer {

        SliceProfile definitionProfile;
        SliceProfile boundaryProfile;
        std::vector<SliceProfile> pathProfiles;

        float toolRadius = 0.0f;
        float stepover = 0.0f;

        void solve() override {
            buildBore();
        }

        void clearBoreData() {
            paths.clear();
            points.clear();
            profiles.clear();

            definitionProfile.clear();
            boundaryProfile.clear();
            pathProfiles.clear();
        }

        void buildBore() {

            clearBoreData();

            definitionProfile = makeProfile();

            if (definitionProfile.empty()) {

                dbg(
                    "[BoreSlice] z=%.3f definition profile empty source=%zu",
                    z,
                    source.size()
                );

                return;
            }

            boundaryProfile = definitionProfile.inset(toolRadius);

            profiles.push_back(definitionProfile);
            profiles.push_back(boundaryProfile);

            dbg(
                "[BoreSlice] z=%.3f definition entries=%zu boundary entries=%zu radius=%.3f stepover=%.3f",
                z,
                definitionProfile.size(),
                boundaryProfile.size(),
                toolRadius,
                stepover
            );

            SliceProfile current = boundaryProfile;

            for (int i = 0; i < 24; i++) {

                pathProfiles.push_back(current);
                collectProfile(current);

                dbg(
                    "[BoreSlice] z=%.3f path=%i entries=%zu outer=%zu holes=%zu paths=%zu",
                    z,
                    i + 1,
                    current.size(),
                    current.outerCount(),
                    current.holeCount(),
                    paths.size()
                );

                current = current.inset(stepover);
            }

            dbg(
                "[BoreSlice] z=%.3f done pathProfiles=%zu paths=%zu",
                z,
                pathProfiles.size(),
                paths.size()
            );
        }
    };
}
