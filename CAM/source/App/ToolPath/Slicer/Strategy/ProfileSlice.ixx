module;

#include <dbg.hpp>

export module Cam.App.Slicer.Strategy.ProfileSlice;

import Cam.App.Slicer.Strategy.Slice.Profile;

import Cam.App.Slicer.Strategy.Slice.Slice;

export namespace Cam::App::Slicer::Strategy {

    using SliceLayer = Slice::Slice;
    using SliceProfile = Slice::Profile;

    struct ProfileSlice : SliceLayer {

        float toolRadius = 0.0f;
        float stepover = 0.0f;

        void solve() override {
            buildProfile();
        }

        void buildProfile() {

            paths.clear();
            points.clear();
            profiles.clear();

            SliceProfile profile = makeProfile();

            if (profile.empty()) {

                dbg(
                    "[ProfileSlice] z=%.3f profile empty source=%zu",
                    z,
                    source.size()
                );

                return;
            }

            profiles.push_back(profile);

            dbg(
                "[ProfileSlice] z=%.3f profile start source=%zu entries=%zu outer=%zu holes=%zu radius=%.3f stepover=%.3f",
                z,
                source.size(),
                profile.size(),
                profile.outerCount(),
                profile.holeCount(),
                toolRadius,
                stepover
            );

            collectProfile(profile);

            // First inset is the tool-center path. Additional insets are
            // roughing passes spaced by stepover.
            //
            // Profile::inset() means "toward material":
            // outer loops inset, hole loops outset.
            float insetAmount = toolRadius;

            for (int i = 0; i < 24; i++) {

                SliceProfile next = profile.inset(insetAmount);

                profiles.push_back(next);
                collectProfile(next);

                dbg(
                    "[ProfileSlice] z=%.3f profile inset=%i entries=%zu outer=%zu holes=%zu paths=%zu",
                    z,
                    i + 1,
                    next.size(),
                    next.outerCount(),
                    next.holeCount(),
                    paths.size()
                );

                profile = next;
                insetAmount = stepover;
            }

            dbg(
                "[ProfileSlice] z=%.3f profile done profiles=%zu paths=%zu",
                z,
                profiles.size(),
                paths.size()
            );
        }
    };
}
