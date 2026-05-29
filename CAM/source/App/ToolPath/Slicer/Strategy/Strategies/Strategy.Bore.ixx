module;

#include <vector>
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
import Cam.App.Slicer.Strategy.Slice.Profile;

export namespace Cam::App::Slicer::Strategy::Strategies {

    using SliceLayer = Slice::Slice;
    using SliceProfile = Slice::Profile;

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
            slice.geometricProfile = SliceProfile(slice.source);

            if (slice.geometricProfile.empty()) {
                return;
            }

            slice.boundaryProfile =
                slice.geometricProfile.inset(toolRadius(ctx));

            slice.profiles.push_back(slice.geometricProfile);
            slice.profiles.push_back(slice.boundaryProfile);

            SliceProfile current = slice.boundaryProfile;
            float stepover = stepoverDistance(ctx);

            for (int i = 0; i < 24; i++) {

                slice.profiles.push_back(current);
                current = current.inset(stepover);
            }
        }

        // Paths
        //--------------------------------------------------

        void buildPaths(const StrategyContext& ctx) {

            paths_.clear();

            for (const SliceLayer& slice : slices_) {

                LayerPath layer;
                layer.z = slice.z;

                for (size_t i = 2; i < slice.profiles.size(); i++) {
                    slice.profiles[i].appendSegments(layer.segments, ctx.climbMilling);
                }

                if (layer.segments.empty()) { continue; }

                paths_.push_back(layer);
            }
        }
    };
}
