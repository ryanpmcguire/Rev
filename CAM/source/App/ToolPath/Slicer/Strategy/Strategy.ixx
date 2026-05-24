module;

#include <vector>
#include <cstddef>

#include <dbg.hpp>

export module Cam.App.Slicer.Strategy.Strategy;

import Rev.Core.Pos;
import Rev.Core.Pos3;
import Rev.Core.Vertex3;

import Cam.App.Model;
import Cam.App.Tool;

import Cam.App.Slicer.Strategy.StrategyType;
import Cam.App.Slicer.Strategy.Slice.Slice;
import Cam.App.Slicer.Strategy.Slice.Segment2;
import Cam.App.Slicer.Strategy.SliceSource;

export namespace Cam::App::Slicer::Strategy {

    using namespace Rev::Core;

    using SliceLayer = Slice::Slice;
    using Segment = Slice::Segment;

    struct LayerPath {

        float z = 0.0f;
        std::vector<Segment> segments;
        std::vector<Pos> points;
    };

    struct StrategyContext {

        const Model* positive = nullptr;
        const Model* negative = nullptr;
        const Tool* tool = nullptr;

        float stepDown = 1.0f;
        float stepover = 0.25f;
    };

    struct Strategy {

        virtual ~Strategy() = default;

        virtual StrategyType type() const = 0;

        void execute(const StrategyContext& ctx) {

            slices_.clear();
            paths_.clear();

            if (!ctx.positive) {
                dbg("[%s] Failed: no positive model", logLabel());
                return;
            }

            if (!ctx.tool) {
                dbg("[%s] Failed: no tool", logLabel());
                return;
            }

            Pos3 min;
            Pos3 max;

            if (!boundsFromModel(*ctx.positive, min, max)) {
                dbg("[%s] Failed: no positive bounds", logLabel());
                return;
            }

            dbg(
                "[%s] Positive bounds min=(%.3f %.3f %.3f), max=(%.3f %.3f %.3f)",
                logLabel(),
                min.x, min.y, min.z,
                max.x, max.y, max.z
            );

            float dz = ctx.stepDown;

            if (dz <= 0.0f) { dz = 1.0f; }

            size_t attempted = 0;
            size_t solved = 0;

            for (float z = min.z; z <= max.z + 1e-4f; z += dz) {
                attempted += 1;

                SliceLayer slice;
                slice.z = z;

                if (!SliceSource::build(*ctx.positive, z, slice)) {
                    dbg("[%s] z=%.3f: no slice source", logLabel(), z);
                    continue;
                }

                processSlice(slice, ctx);

                if (!slice.hasProfiles()) {
                    dbg("[%s] z=%.3f: no profiles", logLabel(), z);
                    continue;
                }

                slices_.push_back(slice);
                solved += 1;
            }

            buildPaths(ctx);

            dbg(
                "[%s] Done. attempted=%zu slices=%zu paths=%zu",
                logLabel(),
                attempted,
                slices_.size(),
                paths_.size()
            );
        }

        const std::vector<SliceLayer>& slices() const {
            return slices_;
        }

        const std::vector<LayerPath>& paths() const {
            return paths_;
        }

        static bool boundsFromModel(
            const Model& model,
            Pos3& min,
            Pos3& max
        ) {
            if (!model.loaded) { return false; }
            if (model.render.triangles.empty()) { return false; }

            bool valid = false;

            for (const Vertex3& v : model.render.triangles) {
                if (!valid) {
                    min = v;
                    max = v;
                    valid = true;
                    continue;
                }

                min = Pos3::min(min, v);
                max = Pos3::max(max, v);
            }

            return valid;
        }

    protected:

        virtual void processSlice(
            SliceLayer& slice,
            const StrategyContext& ctx
        ) = 0;

        virtual void buildPaths(
            const StrategyContext& ctx
        ) = 0;

        static float toolRadius(const StrategyContext& ctx) {
            return static_cast<float>(ctx.tool->radius);
        }

        static float stepoverDistance(const StrategyContext& ctx) {

            float distance =
                static_cast<float>(ctx.tool->diameter) * ctx.stepover;

            if (distance <= 0.0f) {
                distance =
                    static_cast<float>(ctx.tool->diameter) * 0.25f;
            }

            return distance;
        }

        std::vector<SliceLayer> slices_;
        std::vector<LayerPath> paths_;

    private:

        const char* logLabel() const {
            return strategyTypeToString(type()).c_str();
        }
    };
}
