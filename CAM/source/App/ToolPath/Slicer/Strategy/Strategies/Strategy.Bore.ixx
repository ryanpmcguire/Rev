module;

#include <vector>
#include <memory>
#include <cmath>
#include <cstddef>
#include <algorithm>

#include <TopoDS_Face.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GeomAbs_SurfaceType.hxx>

#include <gp_Cylinder.hxx>
#include <gp_Ax1.hxx>
#include <gp_Lin.hxx>

#include <dbg.hpp>

export module Cam.App.Slicer.Strategy.Strategies.Bore;

import Rev.Core.Pos;
import Rev.Core.Pos3;

import Cam.App.Model;
import Cam.App.Slicer.Strategy.Strategy;
import Cam.App.Slicer.Strategy.Slice.Slice;

export namespace Cam::App::Slicer::Strategy::Strategies {

    using Rev::Core::Pos;
    using Rev::Core::Pos3;

    using SliceLayer = Slice::Slice;

    // The BORE strategy: detection is OCC-native (coaxial cylindrical faces);
    // the clearing itself is the shared Geo::SliceStrategy -- a bore is just a
    // region cleared concentrically inward, with no keep-out and no open air.
    struct Bore : Strategy {

        static constexpr const char* name() { return "Bore"; }

        // Detection
        //--------------------------------------------------

        static bool detect(const Model& model) {

            if (!model.loaded) { return false; }
            if (model.faces.empty()) { return false; }

            struct DetectedCylinder {
                gp_Ax1 axis;
                double radius = 0.0;
            };

            auto axesCoaxial = [](const gp_Ax1& a, const gp_Ax1& b, double distanceTolerance, double angularTolerance) {
                if (!a.Direction().IsParallel(b.Direction(), angularTolerance)) { return false; }
                return gp_Lin(a).Distance(gp_Lin(b)) <= distanceTolerance;
            };

            std::vector<DetectedCylinder> cylinders;
            size_t planarCapFaces = 0;

            for (const TopoDS_Face& face : model.faces) {

                BRepAdaptor_Surface surf(face);

                if (surf.GetType() == GeomAbs_Cylinder) {
                    gp_Cylinder cyl = surf.Cylinder();

                    cylinders.push_back({ cyl.Axis(), cyl.Radius() });

                    continue;
                }

                if (surf.GetType() == GeomAbs_Plane) {
                    planarCapFaces += 1;
                    continue;
                }

                return false;
            }

            if (cylinders.empty()) { return false; }

            const double radiusTolerance = 1e-3;
            const double axisDistanceTolerance = 1e-3;
            const double angularTolerance = 1e-2;

            const DetectedCylinder& first = cylinders.front();

            for (const DetectedCylinder& c : cylinders) {

                if (std::abs(c.radius - first.radius) > radiusTolerance) {
                    return false;
                }

                if (!axesCoaxial(c.axis, first.axis, axisDistanceTolerance, angularTolerance)) {
                    return false;
                }
            }

            dbg(
                "[Bore] Detected cylindrical bore: %zu cylindrical face(s), %zu planar face(s), r=%.4f",
                cylinders.size(),
                planarCapFaces,
                first.radius
            );

            return true;
        }

        // Profiles
        //--------------------------------------------------

        void processSlice(SliceLayer& slice, const StrategyContext& ctx) {

            slice.resetProfiles();

            if (slice.source.empty()) { return; }

            const float radius = toolRadius(ctx);

            // Condition: doctrine orientation only. A bore's region is the
            // hole's interior; there is no keep-out section and no open air.
            Geo::Profile seed;
            seed.chains = Geo::Chain::build(slice.source);

            condition(seed);

            Geo::SliceParams params;
            params.kind = Geo::StrategyKind::Profile;
            params.toolRadius = radius;
            params.stepover = (radius > 1e-6f) ? stepoverDistance(ctx) / radius : 1.0f;
            params.maxGenerations = 256;
            params.reverse = false;
            params.climb = ctx.climbMilling;

            Geo::SliceStrategy strategy(params);
            strategy.ingest(std::move(seed));
            strategy.run();

            slice.result = std::move(strategy.result);
        }

        // Paths
        //--------------------------------------------------

        // HELICAL boring: the 2D engine's concentric generations are the
        // scaffolding. The same ring level is paired across every slice and
        // descended as ONE CONTINUOUS HELIX -- one revolution per stepdown,
        // entering at the surface -- finished with a single flat revolution
        // at the bottom to clean the floor. Rings run INNERMOST FIRST, then
        // outward; no retract between slices, only between rings.
        //
        // Storage convention: paths/points are stored in REVERSE execution
        // order (the display flip makes them forward). So rings are stored
        // outermost -> innermost, each ring's layers bottom -> top, and each
        // helical layer's z (stored start) is the LOWER depth with zTo (stored
        // end) the UPPER -- the flip turns it into a descent.
        void buildPaths(const StrategyContext& ctx) {

            paths_.clear();

            if (slices_.empty()) { return; }

            // The surface: top of the delta volume in slice-frame depth.
            // Slices store cut levels only, so the first helix turn ramps in
            // from here.
            float surface = slices_.back().z;

            {
                Pos3 bMin, bMax;

                if (ctx.positive && boundsFromModel(*ctx.positive, bMin, bMax)) {
                    float lo = 0.0f, hi = 0.0f;
                    ctx.frame.depthRange(bMin, bMax, lo, hi);
                    surface = hi;
                }
            }

            // Ring levels common to every slice (generation 0 is the seed).
            size_t rings = static_cast<size_t>(-1);

            for (const SliceLayer& slice : slices_) {
                const size_t g = slice.result.profiles.size();
                rings = std::min(rings, g > 0 ? g - 1 : 0);
            }

            if (rings == 0 || rings == static_cast<size_t>(-1)) { return; }

            const SliceLayer& topSlice = slices_.back();

            // Stored outermost (generation 1) -> innermost (last generation);
            // execution runs the reverse: innermost ring bored first.
            for (size_t g = 1; g <= rings; g++) {

                size_t chainCount = static_cast<size_t>(-1);

                for (const SliceLayer& slice : slices_) {
                    chainCount = std::min(chainCount, slice.result.profiles[g].chains.size());
                }

                if (chainCount == 0 || chainCount == static_cast<size_t>(-1)) { continue; }

                for (size_t j = 0; j < chainCount; j++) {

                    // A common seam for the whole helix: every revolution is
                    // re-seated to the top slice's entry point, so turn N's
                    // end IS turn N+1's start and the spiral never breaks.
                    const Geo::Chain& topChain = topSlice.result.profiles[g].chains[j];

                    if (topChain.edges.empty()) { continue; }

                    const Pos seam = Geo::Chain::eStart(*topChain.edges.front());

                    auto seated = [&](const Geo::Chain& c) {
                        Pos p;
                        size_t k = c.nearestPoint(seam, p);
                        return c.startedAt(k, p);
                    };

                    // The flat floor-cleaning revolution at the bottom slice:
                    // stored FIRST, so it executes LAST.
                    {
                        LayerPath floor;
                        floor.z = slices_.front().z;

                        floor.chains.push_back(seated(slices_.front().result.profiles[g].chains[j]));

                        paths_.push_back(std::move(floor));
                    }

                    // The helix turns, bottom -> top in storage. Each layer
                    // ramps from its own slice depth (stored start = exit of
                    // the executed descent) up to the slice above -- the top
                    // turn ramps to the surface itself.
                    for (size_t s = 0; s < slices_.size(); s++) {

                        LayerPath turn;
                        turn.helical = true;
                        turn.z = slices_[s].z;
                        turn.zTo = (s + 1 < slices_.size()) ? slices_[s + 1].z : surface;

                        turn.chains.push_back(seated(slices_[s].result.profiles[g].chains[j]));

                        paths_.push_back(std::move(turn));
                    }
                }
            }
        }
    };
}
