module;

#include <vector>
#include <cmath>
#include <cstddef>

#include <TopoDS_Face.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GeomAbs_SurfaceType.hxx>

#include <gp_Cylinder.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>

#include <dbg.hpp>

export module Cam.App.Slicer.Strategy.StrategyDetect;

import Cam.App.Model;
import Cam.App.Slicer.Strategy.StrategyType;

export namespace Cam::App::Slicer::Strategy {

    struct DetectedCylinder {
        gp_Dir axis;
        double radius = 0.0;
    };

    // Returns true if every cylindrical face in the model shares the same
    // radius and a parallel axis, and at least one such face exists. Other
    // (non-cylindrical) faces are permitted so that simple drilled-hole
    // deltas — which have planar caps in addition to the cylindrical wall —
    // still register as bores.
    inline bool isCylindricalBore(
        const Model& model,
        double radiusTolerance = 1e-3,
        double angularTolerance = 1e-2
    ) {
        if (!model.loaded) { return false; }
        if (model.faces.empty()) { return false; }

        std::vector<DetectedCylinder> cylinders;

        for (const TopoDS_Face& face : model.faces) {

            BRepAdaptor_Surface surf(face);

            if (surf.GetType() != GeomAbs_Cylinder) {
                continue;
            }

            gp_Cylinder cyl = surf.Cylinder();

            cylinders.push_back({
                cyl.Axis().Direction(),
                cyl.Radius()
            });
        }

        if (cylinders.empty()) { return false; }

        const DetectedCylinder& first = cylinders.front();

        for (const DetectedCylinder& c : cylinders) {

            if (std::abs(c.radius - first.radius) > radiusTolerance) {
                return false;
            }

            if (!c.axis.IsParallel(first.axis, angularTolerance)) {
                return false;
            }
        }

        dbg(
            "[StrategyDetect] Bore detected: %zu cylindrical face(s), r=%.4f",
            cylinders.size(),
            first.radius
        );

        return true;
    }

    inline StrategyType detectStrategy(const Model& positive) {

        if (isCylindricalBore(positive)) {
            return StrategyType::Bore;
        }

        return StrategyType::Hatch;
    }
}
