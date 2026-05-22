module;

#include <vector>
#include <memory>
#include <cstddef>

#include <dbg.hpp>

export module Cam.App.Slicer.Strategy.BoreStrategy;

import Rev.Core.Pos3;

import Cam.App.Model;
import Cam.App.Tool;

import Cam.App.Slicer.Strategy.Strategy;
import Cam.App.Slicer.Strategy.StrategyType;
import Cam.App.Slicer.Strategy.Slice.Slice;
import Cam.App.Slicer.Strategy.ProfileSlice;
import Cam.App.Slicer.Strategy.SliceSource;

export namespace Cam::App::Slicer::Strategy {

    using namespace Rev::Core;

    using SliceLayer = Slice::Slice;

    struct BoreStrategy : Strategy {

        std::vector<std::unique_ptr<SliceLayer>> slices_;

        StrategyType type() const override {
            return StrategyType::Profile;
        }

        const std::vector<std::unique_ptr<SliceLayer>>& slices() const override {
            return slices_;
        }

        void execute(const StrategyContext& ctx) override {

            slices_.clear();

            if (!ctx.positive) {
                dbg("[BoreStrategy] Failed: no positive model");
                return;
            }

            if (!ctx.tool) {
                dbg("[BoreStrategy] Failed: no tool");
                return;
            }

            Pos3 min;
            Pos3 max;

            if (!boundsFromModel(*ctx.positive, min, max)) {
                dbg("[BoreStrategy] Failed: no positive bounds");
                return;
            }

            dbg(
                "[BoreStrategy] Positive bounds min=(%.3f %.3f %.3f), max=(%.3f %.3f %.3f)",
                min.x, min.y, min.z,
                max.x, max.y, max.z
            );

            float dz = ctx.stepDown;

            if (dz <= 0.0f) { dz = 1.0f; }

            const float insetAmount = static_cast<float>(ctx.tool->radius);

            size_t attempted = 0;
            size_t solved = 0;

            for (float z = min.z; z <= max.z + 1e-4f; z += dz) {
                attempted += 1;

                auto slice = std::make_unique<ProfileSlice>();

                slice->z = z;
                slice->insetAmount = insetAmount;

                if (!SliceSource::build(*ctx.positive, z, *slice)) {
                    dbg("[BoreStrategy] z=%.3f: no slice source", z);
                    continue;
                }

                slice->solve();

                if (slice->empty()) {
                    dbg("[BoreStrategy] z=%.3f: empty slice", z);
                    continue;
                }

                slices_.push_back(std::move(slice));
                solved += 1;
            }

            dbg(
                "[BoreStrategy] Done. attempted=%zu solved=%zu slices=%zu",
                attempted,
                solved,
                slices_.size()
            );
        }
    };
}
