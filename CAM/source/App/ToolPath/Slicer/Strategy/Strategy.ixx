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

import Cam.App.Slicer.Strategy.CutFrame;
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

    // Inputs shared by every strategy run.
    struct StrategyContext {

        const Model* positive = nullptr;
        const Model* negative = nullptr;
        const Tool* tool = nullptr;

        float stepDown = 1.0f;
        float stepover = 0.25f;
        bool climbMilling = true;

        CutFrame frame = CutFrame::fromAxis({ 0.0f, 0.0f, 1.0f });
    };

    // Base class for slice/profile/path generation.
    struct Strategy {

        virtual ~Strategy() = default;

        // Run
        //--------------------------------------------------

        template<typename S>
        static void run(S& strategy, const StrategyContext& ctx) {

            strategy.slices_.clear();
            strategy.paths_.clear();

            if (!ctx.positive) {
                dbg("[%s] Failed: no positive model", S::name());
                return;
            }

            if (!ctx.tool) {
                dbg("[%s] Failed: no tool", S::name());
                return;
            }

            Pos3 min;
            Pos3 max;

            if (!boundsFromModel(*ctx.positive, min, max)) {
                dbg("[%s] Failed: no positive bounds", S::name());
                return;
            }

            float minDepth = 0.0f;
            float maxDepth = 0.0f;

            ctx.frame.depthRange(min, max, minDepth, maxDepth);

            dbg(
                "[%s] Positive bounds min=(%.3f %.3f %.3f), max=(%.3f %.3f %.3f)",
                S::name(),
                min.x, min.y, min.z,
                max.x, max.y, max.z
            );

            dbg(
                "[%s] Slice axis=(%.3f %.3f %.3f) depth=%.3f..%.3f",
                S::name(),
                ctx.frame.axis.x, ctx.frame.axis.y, ctx.frame.axis.z,
                minDepth,
                maxDepth
            );

            float dz = ctx.stepDown;

            if (dz <= 0.0f) { dz = 1.0f; }

            size_t attempted = 0;

            for (float depth = minDepth; depth <= maxDepth + 1e-4f; depth += dz) {
                attempted += 1;

                SliceLayer slice;
                slice.z = depth;

                if (!SliceSource::build(*ctx.positive, ctx.frame, depth, slice)) {
                    dbg("[%s] depth=%.3f: no slice source", S::name(), depth);
                    continue;
                }

                strategy.processSlice(slice, ctx);

                if (!slice.hasProfiles()) {
                    dbg("[%s] depth=%.3f: no profiles", S::name(), depth);
                    continue;
                }

                strategy.slices_.push_back(slice);
            }

            strategy.buildPaths(ctx);

            dbg(
                "[%s] Done. attempted=%zu slices=%zu paths=%zu",
                S::name(),
                attempted,
                strategy.slices_.size(),
                strategy.paths_.size()
            );
        }

        // Access
        //--------------------------------------------------

        const std::vector<SliceLayer>& slices() const {
            return slices_;
        }

        const std::vector<LayerPath>& paths() const {
            return paths_;
        }

        // Bounds
        //--------------------------------------------------

        static bool boundsFromModel(const Model& model, Pos3& min, Pos3& max) {
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

        // Helpers
        //--------------------------------------------------

        static float toolRadius(const StrategyContext& ctx) {
            return static_cast<float>(ctx.tool->radius);
        }

        static float stepoverDistance(const StrategyContext& ctx) {

            float distance = static_cast<float>(ctx.tool->diameter) * ctx.stepover;

            if (distance <= 0.0f) {
                distance = static_cast<float>(ctx.tool->diameter) * 0.25f;
            }

            return distance;
        }

        // Output
        std::vector<SliceLayer> slices_;
        std::vector<LayerPath> paths_;
    };
}
