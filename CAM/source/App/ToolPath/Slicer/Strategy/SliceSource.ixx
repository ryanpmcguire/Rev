module;

#include <vector>
#include <cmath>

#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <gp_Circ.hxx>

#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>

#include <BRepAlgoAPI_Section.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <GCPnts_UniformAbscissa.hxx>
#include <GeomAbs_CurveType.hxx>

#include <dbg.hpp>

export module Cam.App.Slicer.Strategy.SliceSource;

import Rev.Core.Pos;
import Rev.Core.Pos3;

import Cam.App.Model;
import Cam.App.Slicer.Strategy.CutFrame;
import Cam.App.Slicer.Strategy.Slice.Segment2;

import Cam.App.Slicer.Strategy.Slice.Slice;

export namespace Cam::App::Slicer::Strategy {

    using namespace Rev::Core;

    using SliceLayer = Slice::Slice;
    using SliceSegment = Slice::Segment;

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

        static bool addOccLine(SliceLayer& slice, BRepAdaptor_Curve& curve, double first, double last, const CutFrame& frame, float depth) {
            slice.addLine(uvFromGp(curve.Value(first), frame, depth), uvFromGp(curve.Value(last), frame, depth));

            return true;
        }

        static bool addOccCircle(SliceLayer& slice, BRepAdaptor_Curve& curve, double first, double last, const CutFrame& frame, float depth) {
            gp_Circ circle = curve.Circle();

            Pos center = uvFromGp(circle.Location(), frame, depth);

            Pos pFirst = uvFromGp(curve.Value(first), frame, depth);
            float radius = (pFirst - center).pythag();

            if (radius <= 1e-6f) { return false; }

            double span = last - first;

            if (std::abs(span) <= 1e-9) { return false; }

            // Polar angle of the edge start in slice (u,v). OCCT curve parameters
            // are not UV polar angles once the slice plane is re-oriented, but the
            // parameter span still equals the true angular sweep along the circle.
            float a0 = frame.uvAngle(center, pFirst);

            Pos pMid = uvFromGp(curve.Value(first + span * 0.5), frame, depth);

            Pos r0 = pFirst - center;
            Pos rMid = pMid - center;

            float cross = r0.x * rMid.y - r0.y * rMid.x;

            float sweep = static_cast<float>(span);

            if (cross < 0.0f) {
                sweep = -sweep;
            }

            float aMid = a0 + sweep * 0.5f;
            float a1 = a0 + sweep;

            slice.addSegment(SliceSegment::Arc(center, radius, a0, aMid));
            slice.addSegment(SliceSegment::Arc(center, radius, aMid, a1));

            return true;
        }

        static bool addAnalytic(SliceLayer& slice, BRepAdaptor_Curve& curve, double first, double last, const CutFrame& frame, float depth) {
            switch (curve.GetType()) {

                case GeomAbs_Line: {
                    return addOccLine(slice, curve, first, last, frame, depth);
                }

                case GeomAbs_Circle: {
                    return addOccCircle(slice, curve, first, last, frame, depth);
                }

                default: {
                    return false;
                }
            }
        }

        // Sampled edges
        //--------------------------------------------------

        static void addSampled(SliceLayer& slice, BRepAdaptor_Curve& curve, double first, double last, const CutFrame& frame, float depth) {
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

                size_t before = slice.source.size();

                GeomAbs_CurveType type = curve.GetType();

                if (addAnalytic(slice, curve, first, last, frame, depth)) {

                    size_t added = slice.source.size() - before;

                    if (type == GeomAbs_Line) { analyticLines += added; }
                    if (type == GeomAbs_Circle) { analyticCircles += added; }
                }
                else {
                    addSampled(slice, curve, first, last, frame, depth);
                    sampledSegments += slice.source.size() - before;
                }
            }

            dbg(
                "[SliceSource] depth=%.3f edges=%zu segments=%zu lines=%zu circles=%zu sampled=%zu",
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
