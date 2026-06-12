module;

#include <vector>
#include <memory>
#include <cmath>
#include <algorithm>
#include <utility>

#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <gp_Circ.hxx>

#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopAbs_Orientation.hxx>

#include <BRepAlgoAPI_Section.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <GCPnts_UniformAbscissa.hxx>
#include <GeomAbs_CurveType.hxx>

#include <dbg.hpp>

export module Cam.App.Slicer.Strategy.SliceSource;

import Rev.Core.Pos;
import Rev.Core.Pos3;

import Geo.Strategy;

import Cam.App.Model;
import Cam.App.Slicer.Strategy.CutFrame;
import Cam.App.Slicer.Strategy.Slice.Slice;

export namespace Cam::App::Slicer::Strategy {

    using namespace Rev::Core;

    using SliceLayer = Slice::Slice;

    // The OCC -> stoicheia frontier: the ONLY place a slice's 2D geometry is
    // anything other than Geo stoicheia. Section edges convert at face value:
    // lines stay lines, partial circles become one Arc2 (chirality in the
    // through-point), full circles become a genuine Circle2 (chirality in the
    // marker), and everything else tessellates into segment runs.
    struct SliceSource {

        // Conversion
        //--------------------------------------------------

        static Pos3 pos3FromGp(const gp_Pnt& p) {
            return {
                static_cast<float>(p.X()),
                static_cast<float>(p.Y()),
                static_cast<float>(p.Z())
            };
        }

        static Pos uvFromGp(const gp_Pnt& p, const CutFrame& frame, float depth) {
            return frame.worldToUv(pos3FromGp(p), depth);
        }

        // Analytic edges
        //--------------------------------------------------

        static bool addOccLine(SliceLayer& slice, BRepAdaptor_Curve& curve, double first, double last, bool reversed, const CutFrame& frame, float depth) {

            Pos a = uvFromGp(curve.Value(first), frame, depth);
            Pos b = uvFromGp(curve.Value(last), frame, depth);

            if (reversed) { std::swap(a, b); }

            slice.addLine(a, b);

            return true;
        }

        static bool addOccCircle(SliceLayer& slice, BRepAdaptor_Curve& curve, double first, double last, bool reversed, const CutFrame& frame, float depth) {
            gp_Circ circle = curve.Circle();

            Pos center = uvFromGp(circle.Location(), frame, depth);

            Pos pFirst = uvFromGp(curve.Value(first), frame, depth);
            float radius = (pFirst - center).pythag();

            if (radius <= 1e-6f) { return false; }

            double span = last - first;

            if (std::abs(span) <= 1e-9) { return false; }

            // Orientation of the sweep in UV, probed at the QUARTER point -- never
            // the midpoint: cross(r0, rMid) ~ sin(span/2), which is ZERO for a
            // full circle (span = 2*pi), making the sign of the test pure float
            // noise -- full circles came out with random directionality. The
            // quarter point's cross ~ sin(span/4) is strictly positive for every
            // span up to 4*pi, and maximal exactly at the full-circle case.
            Pos pQuarter = uvFromGp(curve.Value(first + span * 0.25), frame, depth);

            Pos r0 = pFirst - center;
            Pos rQ = pQuarter - center;

            float cross = r0.x * rQ.y - r0.y * rQ.x;

            int chir = (cross >= 0.0f) ? 1 : -1;

            // A REVERSED topological edge is traced last -> first: the true
            // travel runs the OPPOSITE way round to the curve's parameter.
            if (reversed) { chir = -chir; }

            // A FULL TURN (a true circle, or an arc that closes on itself) is
            // handed off as TWO NEIGHBOURING HALF-ARCS, never a Circle2: the
            // slicing hand-off downstream is blind to whole closed curves, but
            // two arcs chain into an ordinary closed loop. Each half is traced
            // a -> b through its own quarter point, so the travel direction
            // (chir, already corrected for a REVERSED edge) lives entirely in
            // the geometry.
            if (std::abs(span) >= Geo::TAU - 1e-3) {

                float ang0 = frame.uvAngle(center, pFirst);
                float s = (chir > 0) ? 1.0f : -1.0f;

                auto at = [&](float ang) {
                    return center + Pos(std::cos(ang), std::sin(ang)) * radius;
                };

                Pos p0 = at(ang0);
                Pos pHalf = at(ang0 + s * Geo::TAU * 0.5f);

                slice.addEdge(std::make_unique<Geo::Arc2>(center, p0, pHalf, at(ang0 + s * Geo::TAU * 0.25f)));
                slice.addEdge(std::make_unique<Geo::Arc2>(center, pHalf, p0, at(ang0 + s * Geo::TAU * 0.75f)));

                return true;
            }

            // A partial circle is a single Arc2, ALWAYS traced a -> b through
            // the mid-sweep point d: handedness lives in the geometry itself.
            Pos pLast = uvFromGp(curve.Value(last), frame, depth);
            Pos pMid = uvFromGp(curve.Value(first + span * 0.5), frame, depth);

            // Re-seat all three exactly on the circle so the arc is true.
            Pos a = center + (pFirst - center).normalized() * radius;
            Pos b = center + (pLast - center).normalized() * radius;
            Pos d = center + (pMid - center).normalized() * radius;

            // Reversed travel: b -> a, still through the same mid-sweep d.
            if (reversed) { std::swap(a, b); }

            slice.addEdge(std::make_unique<Geo::Arc2>(center, a, b, d));

            return true;
        }

        static bool addAnalytic(SliceLayer& slice, BRepAdaptor_Curve& curve, double first, double last, bool reversed, const CutFrame& frame, float depth) {
            switch (curve.GetType()) {

                case GeomAbs_Line: {
                    return addOccLine(slice, curve, first, last, reversed, frame, depth);
                }

                case GeomAbs_Circle: {
                    return addOccCircle(slice, curve, first, last, reversed, frame, depth);
                }

                default: {
                    return false;
                }
            }
        }

        // Sampled edges
        //--------------------------------------------------

        static void addSampled(SliceLayer& slice, BRepAdaptor_Curve& curve, double first, double last, bool reversed, const CutFrame& frame, float depth) {
            std::vector<Pos> sampled;

            double lengthStep = 0.25;

            GCPnts_UniformAbscissa sampler(curve, lengthStep, first, last);

            if (sampler.IsDone() && sampler.NbPoints() >= 2) {

                for (int i = 1; i <= sampler.NbPoints(); i++) {
                    sampled.push_back(uvFromGp(curve.Value(sampler.Parameter(i)), frame, depth));
                }
            }
            else {

                int samples = 12;

                for (int i = 0; i <= samples; i++) {
                    double u = first + (last - first) * (double(i) / double(samples));
                    sampled.push_back(uvFromGp(curve.Value(u), frame, depth));
                }
            }

            if (reversed) { std::reverse(sampled.begin(), sampled.end()); }

            for (size_t i = 0; i + 1 < sampled.size(); i++) {
                slice.addLine(sampled[i], sampled[i + 1]);
            }

            if (curve.IsClosed() && sampled.size() >= 2) {
                slice.addLine(sampled.back(), sampled.front());
            }
        }

        // Build
        //--------------------------------------------------

        static bool build(const Model& model, const CutFrame& frame, float depth, SliceLayer& slice) {
            slice.z = depth;

            if (!model.loaded) { return false; }
            if (model.shape.IsNull()) { return false; }

            Pos3 planeOrigin = frame.planeOrigin(depth);

            gp_Pln plane(
                gp_Pnt(planeOrigin.x, planeOrigin.y, planeOrigin.z),
                gp_Dir(frame.axis.x, frame.axis.y, frame.axis.z)
            );

            BRepAlgoAPI_Section section(model.shape, plane, false);

            section.ComputePCurveOn1(true);
            section.Approximation(true);
            section.Build();

            if (!section.IsDone()) { return false; }

            TopoDS_Shape sectionShape = section.Shape();

            if (sectionShape.IsNull()) { return false; }

            size_t edges = 0;
            size_t analyticLines = 0;
            size_t analyticCircles = 0;
            size_t sampledSegments = 0;

            for (TopExp_Explorer exp(sectionShape, TopAbs_EDGE); exp.More(); exp.Next()) {
                edges += 1;

                TopoDS_Edge edge = TopoDS::Edge(exp.Current());

                BRepAdaptor_Curve curve(edge);

                double first = curve.FirstParameter();
                double last = curve.LastParameter();

                if (last <= first) { continue; }

                // A REVERSED topological edge travels its curve last -> first;
                // ignoring this leaves edge directions arbitrary -- and a lone
                // full circle never gets corrected by chain linking.
                bool reversed = (edge.Orientation() == TopAbs_REVERSED);

                size_t before = slice.source.size();

                GeomAbs_CurveType type = curve.GetType();

                if (addAnalytic(slice, curve, first, last, reversed, frame, depth)) {

                    size_t added = slice.source.size() - before;

                    if (type == GeomAbs_Line) { analyticLines += added; }
                    if (type == GeomAbs_Circle) { analyticCircles += added; }
                }
                else {
                    addSampled(slice, curve, first, last, reversed, frame, depth);
                    sampledSegments += slice.source.size() - before;
                }
            }

            dbg(
                "[SliceSource] depth=%.3f edges=%zu stoicheia=%zu lines=%zu circles=%zu sampled=%zu",
                depth,
                edges,
                slice.source.size(),
                analyticLines,
                analyticCircles,
                sampledSegments
            );

            return !slice.source.empty();
        }
    };
}
