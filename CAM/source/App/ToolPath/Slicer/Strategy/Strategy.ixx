module;

#include <vector>
#include <cstddef>
#include <cmath>

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
import Cam.App.Slicer.Strategy.Slice.Profile;
import Cam.App.Slicer.Strategy.SliceSource;

export namespace Cam::App::Slicer::Strategy {

    using namespace Rev::Core;

    using SliceLayer = Slice::Slice;
    using Segment = Slice::Segment;
    using SliceProfile = Slice::Profile;

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

        // Concentric clearing
        //--------------------------------------------------

        // Generous upper bound; real termination comes from the degeneracy /
        // finality checks below, not this cap.
        static constexpr int MaxOffsetPasses = 256;

        // Append successive inward offsets of `start` to the slice.  Each pass
        // is judged before it is emitted:
        //
        //   * Unfit (self-intersecting) ring — a bowtie / spike the naive
        //     offset produced near the medial axis.  We do NOT emit it, but we
        //     keep shrinking, because the next inset often recovers a clean
        //     inner ring.  A short skip budget ends the slice if it never does.
        //   * Collapsed (simple ring, ~zero area) — nothing meaningful left.
        //     Terminal.
        //   * Too little area — a fit ring smaller than one pass can clear is
        //     kept as the final ring, then we stop.
        //   * Negative-area inversion / stalled offset — also terminal.  (Area-
        //     based tests are only trusted on a fit ring; a self-intersecting
        //     ring has near-zero net area and would fool them.)
        void appendConcentricInsets(
            SliceLayer& slice,
            const StrategyContext& ctx,
            const SliceProfile& start
        ) {
            const float stepover = stepoverDistance(ctx);
            const float minPassArea = stepover * stepover;

            constexpr int MaxConsecutiveSkips = 4;
            int skips = 0;

            SliceProfile current = start;

            for (int i = 0; i < MaxOffsetPasses; i++) {

                if (current.empty()) { break; }

                const bool fit = !current.hasSelfIntersectingChain();

                if (fit) {

                    // A fit ring with no area left is terminal.
                    if (current.hasCollapsedChain()) { break; }

                    skips = 0;
                    slice.profiles.push_back(current);

                    // Too little area to host another distinct pass — final ring.
                    if (std::abs(current.signedAreaSum()) <= minPassArea) { break; }
                }
                else if (++skips > MaxConsecutiveSkips) {
                    break;
                }

                SliceProfile next = current.inset(stepover);

                if (next.empty()) { break; }

                if (SliceProfile::signFlipped(
                        current.signedAreaSum(),
                        next.signedAreaSum()
                )) {
                    break;
                }

                // Stalled-offset progress check — only meaningful for a fit ring
                // (a self-intersecting ring's lobes cancel to ~zero net area).
                if (fit &&
                    std::abs(next.signedAreaSum()) >= std::abs(current.signedAreaSum()) * 0.999f) {
                    break;
                }

                current = next;
            }
        }

        // Output
        std::vector<SliceLayer> slices_;
        std::vector<LayerPath> paths_;
    };
}
