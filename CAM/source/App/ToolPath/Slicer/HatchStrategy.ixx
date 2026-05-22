module;

#include <vector>
#include <memory>
#include <cstddef>

#include <dbg.hpp>

export module Cam.App.ToolPath.Slicer.HatchStrategy;

import Rev.Core.Pos3;

import Cam.App.Model;
import Cam.App.Tool;

import Cam.App.ToolPath.Slicer.Strategy;
import Cam.App.ToolPath.Slicer.StrategyType;
import Cam.App.ToolPath.Slicer.Slice;
import Cam.App.ToolPath.Slicer.HatchSlice;
import Cam.App.ToolPath.Slicer.SliceSource;

export namespace Cam::App::ToolPath::Slicer {

    using namespace Rev::Core;

    struct HatchStrategy : Strategy {

        std::vector<std::unique_ptr<Slice>> slices_;

        StrategyType type() const override {
            return StrategyType::Hatch;
        }

        const std::vector<std::unique_ptr<Slice>>& slices() const override {
            return slices_;
        }

        void execute(const StrategyContext& ctx) override {

            slices_.clear();

            if (!ctx.positive) {
                dbg("[HatchStrategy] Failed: no positive model");
                return;
            }

            if (!ctx.tool) {
                dbg("[HatchStrategy] Failed: no tool");
                return;
            }

            Pos3 min;
            Pos3 max;

            if (!boundsFromModel(*ctx.positive, min, max)) {
                dbg("[HatchStrategy] Failed: no positive bounds");
                return;
            }

            dbg(
                "[HatchStrategy] Positive bounds min=(%.3f %.3f %.3f), max=(%.3f %.3f %.3f)",
                min.x, min.y, min.z,
                max.x, max.y, max.z
            );

            float dz = ctx.stepDown;

            if (dz <= 0.0f) { dz = 1.0f; }

            size_t attempted = 0;
            size_t solved = 0;

            for (float z = min.z; z <= max.z + 1e-4f; z += dz) {
                attempted += 1;

                auto slice = std::make_unique<HatchSlice>();

                slice->z = z;

                if (!SliceSource::build(*ctx.positive, z, *slice)) {
                    dbg("[HatchStrategy] z=%.3f: no slice source", z);
                    continue;
                }

                slice->solve();

                if (slice->empty()) {
                    dbg("[HatchStrategy] z=%.3f: empty slice", z);
                    continue;
                }

                slices_.push_back(std::move(slice));
                solved += 1;
            }

            dbg(
                "[HatchStrategy] Done. attempted=%zu solved=%zu slices=%zu",
                attempted,
                solved,
                slices_.size()
            );
        }
    };
}
