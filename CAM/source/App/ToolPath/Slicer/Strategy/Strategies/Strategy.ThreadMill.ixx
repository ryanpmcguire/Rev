module;

#include <vector>
#include <memory>
#include <cmath>
#include <cstddef>
#include <algorithm>

#include <dbg.hpp>

export module Cam.App.Slicer.Strategy.Strategies.ThreadMill;

import Rev.Core.Pos;
import Rev.Core.Pos3;

import Cam.App.Model;
import Cam.App.Tool;
import Cam.App.Slicer.Strategy.Strategy;
import Cam.App.Slicer.Strategy.Slice.Slice;

export namespace Cam::App::Slicer::Strategy::Strategies {

    using Rev::Core::Pos;
    using Rev::Core::Pos3;

    using SliceLayer = Slice::Slice;

    // Thread milling = a BORE that helixes instead of clearing.  It consumes a
    // profile (a cylinder, or ANY closed profile) exactly like Bore/Profile do:
    // it slices the input and runs the shared slice strategy's BOUNDARY pass to
    // get the tool-centre path one tool radius off the wall.  Then, instead of
    // stepping concentric rings inward, it sweeps that single boundary loop down
    // as a HELIX -- one revolution per thread pitch over the feature depth.  The
    // profile defines the thread's size and shape; the only parameter that makes
    // it a thread rather than a bore is the PITCH.
    struct ThreadMill : Strategy {

        static constexpr const char* name() { return "ThreadMill"; }

        static bool detect(const Model&) { return false; }

        // The centroid of a loop, from its edge start points -- the hole axis in
        // the slice plane.
        static Pos loopCentroid(const Geo::Chain& loop) {
            double sx = 0.0, sy = 0.0;
            int n = 0;
            for (const auto& e : loop.edges) {
                const Pos p = Geo::Chain::eStart(*e);
                sx += p.x; sy += p.y; n++;
            }
            if (n == 0) { return Pos(0.0f, 0.0f); }
            return Pos(float(sx / n), float(sy / n));
        }

        // A single-circle tool-centre loop at `centre`, radius `r`, wound to match
        // `sign` (+1 CCW, -1 CW) so it carries the same climb handedness the slice
        // boundary already chose.
        static Geo::Chain circleLoop(const Pos& centre, float r, int sign) {
            Geo::Chain ring;
            ring.closed = true;
            ring.edges.push_back(std::make_unique<Geo::Circle2>(centre, r));   // CCW
            if (sign < 0) { ring = ring.reversed(); }
            return ring;
        }

        // Condition + boundary pass, identical to the Profile/Bore front end --
        // we only keep generation 1 (the wall-following ring).
        void processSlice(SliceLayer& slice, const StrategyContext& ctx) {

            slice.resetProfiles();

            if (slice.source.empty()) { return; }

            const float radius = static_cast<float>(ctx.tool->radius);

            Geo::Profile seed;
            seed.chains = Geo::Chain::build(slice.source);

            condition(seed);

            Geo::SliceParams params;
            params.kind = Geo::StrategyKind::Profile;
            params.toolRadius = radius;
            params.stepover = 1.0f;
            params.maxGenerations = 1;          // just the boundary: the thread-wall path
            params.reverse = false;
            params.climb = ctx.climbMilling;

            Geo::SliceStrategy strategy(params);
            strategy.ingest(std::move(seed));
            strategy.run();

            slice.result = std::move(strategy.result);
        }

        void buildPaths(const StrategyContext& ctx) {

            paths_.clear();

            if (slices_.empty()) { return; }

            const float pitch = (ctx.threadPitch > 1e-4f) ? ctx.threadPitch : 0.4f;

            // The thread's axial extent, from the slices we kept.
            float zTop = slices_.front().z;
            float zBot = slices_.front().z;
            for (const SliceLayer& s : slices_) {
                zTop = std::max(zTop, s.z);
                zBot = std::min(zBot, s.z);
            }

            const float depth = zTop - zBot;
            if (depth <= 1e-4f) { return; }

            // The cross-section the cutter follows: the boundary ring from the
            // top-most slice that produced one.  (Threads are constant-section,
            // so any slice's boundary is the same loop.)
            const SliceLayer* crossSlice = nullptr;
            for (const SliceLayer& s : slices_) {
                if (s.result.profiles.size() > 1 && !s.result.profiles[1].chains.empty()) {
                    if (!crossSlice || s.z > crossSlice->z) { crossSlice = &s; }
                }
            }

            if (!crossSlice) { return; }

            const Geo::Profile& boundary = crossSlice->result.profiles[1];

            // A thread is defined ENTIRELY by its callout -- we KNOW the crest is at
            // the major diameter about the hole axis, regardless of the delta volume
            // the operation happened to leave (the pre-bore could be anything; the
            // hole as drawn could be enormous).  So we do NOT trace the sliced
            // collar wall.  We take only the hole's AXIS and HANDEDNESS from each
            // boundary loop (its centroid and turning sign -- the latter already
            // encodes the climb choice), and lay a fresh tool-centre circle at the
            // callout radius: r = majorR - toolR for an internal thread.
            //
            // For an external thread (not yet handled) or a missing callout we fall
            // back to the sliced boundary itself.
            Geo::Profile cutRing;

            const float majorR = 0.5f * ctx.threadMajorDiameter;
            const float toolR  = static_cast<float>(ctx.tool->radius);
            const float r      = majorR - toolR;

            if (ctx.threadInternal && majorR > 1e-4f && r > 1e-4f) {
                for (const Geo::Chain& src : boundary.chains) {
                    if (!src.closed || src.edges.empty()) { continue; }
                    cutRing.chains.push_back(
                        circleLoop(loopCentroid(src), r, src.turningSign())
                    );
                }
            }

            if (cutRing.chains.empty()) { cutRing = boundary.clone(); }

            // Revolutions to form the thread.  A SINGLE-POINT cutter (one tooth)
            // helically interpolates the whole length: one revolution per pitch.
            // A multi-row FORM cutter has `toothCount` teeth one pitch apart, so it
            // cuts that many thread turns at once -- its tooth stack already spans
            // (toothCount - 1) extra pitches above the tip.  As the tip spirals up
            // continuously, those stacked teeth cover the rest, so the helix only
            // needs to sweep the turns the stack does NOT span.  Once the teeth
            // cover the whole feature this collapses to a single orbit.
            const int turnsTotal = std::max(1, static_cast<int>(std::ceil(depth / pitch)));

            int teeth = 1;
            if (ctx.tool && ctx.tool->type == Cam::App::Tool::Type::ThreadMill) {
                teeth = std::max(1, ctx.tool->threadMill.toothCount);
            }

            const int turns = std::max(1, turnsTotal - (teeth - 1));

            // Every closed boundary loop becomes its own helix.  For a single
            // hole that is one helix; "any profile" with several loops threads
            // each.  The loops already carry the correct climb handedness from
            // the boundary pass.
            for (const Geo::Chain& loop : cutRing.chains) {

                if (!loop.closed || loop.edges.empty()) { continue; }

                // The bore AXIS and the helix SEAM (where the ring opens).  All
                // plunging and retracting happens strictly at the axis: the tool
                // drops down the open centre, leads radially out to the seam, cuts
                // the helix, then leads back to the centre before lifting.  This
                // keeps every vertical move clear of the thread wall.
                const Pos centre = loopCentroid(loop);
                const Pos seam   = Geo::Chain::eStart(*loop.edges.front());

                // The revolutions in EXECUTION order, as a continuous spiral
                // (each turn ends where the next begins) -- up-cut climbs from the
                // bottom, down-cut descends from the top.
                struct Turn { float z; float zTo; };
                std::vector<Turn> seq;
                seq.reserve(static_cast<size_t>(turns));

                for (int t = 0; t < turns; t++) {
                    if (ctx.threadUpCut) {
                        const float a = zBot + pitch * float(t);
                        const float b = std::min(zTop, zBot + pitch * float(t + 1));
                        seq.push_back({ a, b });
                    }
                    else {
                        const float a = std::max(zBot, zTop - pitch * float(t));
                        const float b = std::max(zBot, zTop - pitch * float(t + 1));
                        seq.push_back({ a, b });
                    }
                }

                const float entryDepth = seq.front().z;
                const float exitDepth  = seq.back().zTo;

                // The point builder consumes layers back-to-front, so push in
                // REVERSE execution order: lead-out first (runs last), the spiral
                // next (replayed forward), the lead-in last (runs first).  Lead
                // moves are flat point-polylines at the cut depth -- the vertical
                // plunge/retract before/after them lands on the centre.

                // Lead-out: seam -> centre at the spiral's top/bottom.
                {
                    LayerPath leadOut;
                    leadOut.z = exitDepth;
                    leadOut.points = { seam, centre };
                    paths_.push_back(std::move(leadOut));
                }

                for (int i = static_cast<int>(seq.size()); i-- > 0; ) {
                    LayerPath turn;
                    turn.helical = true;
                    turn.z = seq[i].z;
                    turn.zTo = seq[i].zTo;
                    turn.chains.push_back(loop.clone());
                    paths_.push_back(std::move(turn));
                }

                // Lead-in: centre -> seam at the spiral's start depth.
                {
                    LayerPath leadIn;
                    leadIn.z = entryDepth;
                    leadIn.points = { centre, seam };
                    paths_.push_back(std::move(leadIn));
                }
            }

            dbg(
                "[ThreadMill] pitch=%.3f depth=%.3f turns=%d/%d teeth=%d loops=%zu majorDia=%.3f (%s)",
                pitch, depth, turns, turnsTotal, teeth, cutRing.chains.size(), ctx.threadMajorDiameter,
                ctx.threadUpCut ? "up-cut" : "down-cut"
            );
        }
    };
}
