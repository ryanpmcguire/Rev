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
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>

#include <dbg.hpp>

export module Cam.App.Slicer.Strategy.StrategyDetect;

import Cam.App.Model;
import Cam.App.Slicer.Strategy.StrategyType;

export namespace Cam::App::Slicer::Strategy {

    struct DetectedCylinder {
        gp_Ax1 axis;
        double radius = 0.0;
    };

    enum class BoreCapKind {
        Through,
        Pocket,
        Capped
    };

    inline const char* boreCapKindName(BoreCapKind kind) {

        switch (kind) {

            case BoreCapKind::Through: { return "through"; }
            case BoreCapKind::Pocket: { return "pocket"; }
            case BoreCapKind::Capped: { return "capped"; }
        }

        return "unknown";
    }

    inline bool axesCoaxial(
        const gp_Ax1& a,
        const gp_Ax1& b,
        double distanceTolerance,
        double angularTolerance
    ) {
        if (!a.Direction().IsParallel(b.Direction(), angularTolerance)) {
            return false;
        }

        return gp_Lin(a).Distance(gp_Lin(b)) <= distanceTolerance;
    }

    // Returns true if the model contains one cylindrical bore volume. Through
    // bores may have no planar cap, pocket bores have one, and capped cylinder
    // deltas may have two. Any cylindrical face must be coaxial with the first
    // one and have the same radius.
    inline bool isCylindricalBore(
        const Model& model,
        double radiusTolerance = 1e-3,
        double axisDistanceTolerance = 1e-3,
        double angularTolerance = 1e-2
    ) {
        if (!model.loaded) { return false; }
        if (model.faces.empty()) { return false; }

        std::vector<DetectedCylinder> cylinders;
        size_t planarCapFaces = 0;

        for (const TopoDS_Face& face : model.faces) {

            BRepAdaptor_Surface surf(face);

            if (surf.GetType() == GeomAbs_Cylinder) {
                gp_Cylinder cyl = surf.Cylinder();

                cylinders.push_back({
                    cyl.Axis(),
                    cyl.Radius()
                });

                continue;
            }

            if (surf.GetType() == GeomAbs_Plane) {
                planarCapFaces += 1;
                continue;
            }

            return false;
        }

        if (cylinders.empty()) { return false; }

        const DetectedCylinder& first = cylinders.front();

        for (const DetectedCylinder& c : cylinders) {

            if (std::abs(c.radius - first.radius) > radiusTolerance) {
                return false;
            }

            if (!axesCoaxial(
                c.axis,
                first.axis,
                axisDistanceTolerance,
                angularTolerance
            )) {
                return false;
            }
        }

        BoreCapKind capKind = BoreCapKind::Capped;

        if (planarCapFaces == 0) {
            capKind = BoreCapKind::Through;
        }

        else if (planarCapFaces == 1) {
            capKind = BoreCapKind::Pocket;
        }

        dbg(
            "[StrategyDetect] Bore detected: %s, %zu cylindrical face(s), %zu planar face(s), r=%.4f",
            boreCapKindName(capKind),
            cylinders.size(),
            planarCapFaces,
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
