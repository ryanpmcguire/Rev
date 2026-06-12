module;

#include <vector>
#include <memory>
#include <cmath>
#include <cstddef>

#include <TopoDS_Face.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GeomAbs_SurfaceType.hxx>

#include <gp_Cylinder.hxx>
#include <gp_Ax1.hxx>
#include <gp_Lin.hxx>

#include <dbg.hpp>

export module Cam.App.Slicer.Strategy.Strategies.Bore;

import Cam.App.Model;
import Cam.App.Slicer.Strategy.Strategy;
import Cam.App.Slicer.Strategy.Slice.Slice;

export namespace Cam::App::Slicer::Strategy::Strategies {

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

        void buildPaths(const StrategyContext& ctx) {

            paths_.clear();

            for (const SliceLayer& slice : slices_) {

                if (slice.result.toolpath.empty()) { continue; }

                LayerPath layer;
                layer.z = slice.z;

                for (const Geo::Chain& c : slice.result.toolpath) {
                    layer.chains.push_back(c.clone());
                }

                paths_.push_back(std::move(layer));
            }
        }
    };
}
