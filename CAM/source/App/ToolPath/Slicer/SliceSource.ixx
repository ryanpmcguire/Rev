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

export module Cam.App.Slicer.SliceSource;

import Rev.Core.Pos;

import Cam.App.Model;
import Cam.App.Geometry.Segment;

import Cam.App.Slicer.Slice;

export namespace Cam::App::Slicer {

    using namespace Rev::Core;
    using namespace Cam::App::Geometry;

    struct SliceSource {

        static Pos posFromGp(const gp_Pnt& p) {

            return {
                static_cast<float>(p.X()),
                static_cast<float>(p.Y())
            };
        }

        static bool addOccLine(
            Slice& slice,
            BRepAdaptor_Curve& curve,
            double first,
            double last
        ) {
            slice.addLine(
                posFromGp(curve.Value(first)),
                posFromGp(curve.Value(last))
            );

            return true;
        }

        static bool addOccCircle(
            Slice& slice,
            BRepAdaptor_Curve& curve,
            double first,
            double last
        ) {
            gp_Circ circle = curve.Circle();

            Pos center = posFromGp(
                circle.Location()
            );

            float radius = static_cast<float>(
                circle.Radius()
            );

            if (radius <= 1e-6f) { return false; }

            double span = last - first;

            if (std::abs(span) <= 1e-9) { return false; }

            double mid = first + span * 0.5;

            slice.addSegment(
                Segment::Arc(
                    center,
                    radius,
                    static_cast<float>(first),
                    static_cast<float>(mid)
                )
            );

            slice.addSegment(
                Segment::Arc(
                    center,
                    radius,
                    static_cast<float>(mid),
                    static_cast<float>(last)
                )
            );

            return true;
        }

        static bool addAnalytic(
            Slice& slice,
            BRepAdaptor_Curve& curve,
            double first,
            double last
        ) {
            switch (curve.GetType()) {

                case GeomAbs_Line: {
                    return addOccLine(slice, curve, first, last);
                }

                case GeomAbs_Circle: {
                    return addOccCircle(slice, curve, first, last);
                }

                default: {
                    return false;
                }
            }
        }

        static void addSampled(
            Slice& slice,
            BRepAdaptor_Curve& curve,
            double first,
            double last
        ) {
            std::vector<Pos> sampled;

            double lengthStep = 0.25;

            GCPnts_UniformAbscissa sampler(
                curve,
                lengthStep,
                first,
                last
            );

            if (sampler.IsDone() && sampler.NbPoints() >= 2) {

                for (int i = 1; i <= sampler.NbPoints(); i++) {

                    sampled.push_back(
                        posFromGp(
                            curve.Value(
                                sampler.Parameter(i)
                            )
                        )
                    );
                }
            }

            else {

                int samples = 12;

                for (int i = 0; i <= samples; i++) {

                    double u = first + (last - first) * (
                        double(i) / double(samples)
                    );

                    sampled.push_back(
                        posFromGp(
                            curve.Value(u)
                        )
                    );
                }
            }

            for (size_t i = 0; i + 1 < sampled.size(); i++) {
                slice.addLine(
                    sampled[i],
                    sampled[i + 1]
                );
            }

            if (curve.IsClosed() && sampled.size() >= 2) {
                slice.addLine(
                    sampled.back(),
                    sampled.front()
                );
            }
        }

        static bool build(
            const Model& model,
            float z,
            Slice& slice
        ) {
            slice.z = z;

            if (!model.loaded) { return false; }
            if (model.shape.IsNull()) { return false; }

            gp_Pln plane(
                gp_Pnt(0.0, 0.0, double(z)),
                gp_Dir(0.0, 0.0, 1.0)
            );

            BRepAlgoAPI_Section section(
                model.shape,
                plane,
                false
            );

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

            for (
                TopExp_Explorer exp(sectionShape, TopAbs_EDGE);
                exp.More();
                exp.Next()
            ) {
                edges += 1;

                TopoDS_Edge edge = TopoDS::Edge(
                    exp.Current()
                );

                BRepAdaptor_Curve curve(edge);

                double first = curve.FirstParameter();
                double last = curve.LastParameter();

                if (last <= first) { continue; }

                size_t before = slice.source.size();

                GeomAbs_CurveType type = curve.GetType();

                if (addAnalytic(slice, curve, first, last)) {

                    size_t added = slice.source.size() - before;

                    if (type == GeomAbs_Line) {
                        analyticLines += added;
                    }

                    if (type == GeomAbs_Circle) {
                        analyticCircles += added;
                    }
                }

                else {
                    addSampled(slice, curve, first, last);
                    sampledSegments += slice.source.size() - before;
                }
            }

            dbg(
                "[SliceSource] z=%.3f edges=%zu segments=%zu lines=%zu circles=%zu sampled=%zu",
                z,
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
