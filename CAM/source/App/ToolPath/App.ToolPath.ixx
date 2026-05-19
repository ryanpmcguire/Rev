module;

#include <vector>
#include <string>
#include <cstddef>
#include <algorithm>
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

export module Cam.App.ToolPath;

import Rev.Core.Vertex3;
import Rev.Core.Color;
import Rev.Core.Pos;
import Rev.Core.Pos3;
import Rev.Core.Segment;

import Cam.App.Model;

export namespace Cam::App {

    using namespace Rev::Core;

    struct Tool {
        enum class Kind { Cylinder };

        Kind kind = Kind::Cylinder;

        std::string name = "1mm x 100mm God Tool";

        double diameter = 1.0;
        double radius = 0.5;
        double length = 100.0;

        Pos3 axis = { 0.0f, 0.0f, 1.0f };

        static Tool GodTool() {
            Tool tool;

            tool.kind = Kind::Cylinder;
            tool.name = "1mm x 100mm God Tool";
            tool.diameter = 1.0;
            tool.radius = 0.5;
            tool.length = 100.0;
            tool.axis = { 0.0f, 0.0f, 1.0f };

            return tool;
        }
    };

    struct ToolPathPoint {
        Pos3 position = {};
        double t = 0.0;
        bool rapid = false;
        bool cutting = true;
    };

    enum class BoundaryKind { Air, Part };

    struct SliceSegment {
        Segment segment;
        BoundaryKind kind = BoundaryKind::Air;
    };

    struct SliceCrossing {
        float x = 0.0f;
        BoundaryKind kind = BoundaryKind::Air;
    };

    struct ToolPathSlice {
        float z = 0.0f;

        Pos min = {};
        Pos max = {};

        std::vector<SliceSegment> carve;
        std::vector<SliceSegment> avoid;
        std::vector<Pos> points;

        bool valid = false;

        // State
        //--------------------------------------------------

        void clear() {
            carve.clear();
            avoid.clear();
            points.clear();

            min = {};
            max = {};
            valid = false;
        }

        static Pos componentMin(const Pos& a, const Pos& b) { return { std::min(a.x, b.x), std::min(a.y, b.y) }; }
        static Pos componentMax(const Pos& a, const Pos& b) { return { std::max(a.x, b.x), std::max(a.y, b.y) }; }

        Pos posFromGp(const gp_Pnt& p) const { return { static_cast<float>(p.X()), static_cast<float>(p.Y()) }; }

        void includePoint(const Pos& p) {
            if (!valid) { min = p; max = p; valid = true; return; }

            min = componentMin(min, p);
            max = componentMax(max, p);
        }

        void includeSegment(const Segment& s, int samples = 24) {
            if (s.kind == Segment::Type::Line) {
                includePoint(s.a);
                includePoint(s.b);
                return;
            }

            for (int i = 0; i <= samples; i++) {
                includePoint(s.at(float(i) / float(samples)));
            }
        }

        // Geometry helpers
        //--------------------------------------------------

        bool segmentNearAvoid(const Segment& segment, float tolerance = 0.05f) {
            Pos mid = segment.at(0.5f);

            for (const SliceSegment& s : avoid) {
                if (s.segment.distanceTo(mid) <= tolerance) { return true; }
                if (segment.distanceTo(s.segment) <= tolerance) { return true; }
            }

            return false;
        }

        // Section segments
        //--------------------------------------------------

        void addSegment(std::vector<SliceSegment>& out, const Segment& segment, BoundaryKind kind = BoundaryKind::Air) {
            if (segment.kind == Segment::Type::Line && segment.chordLength() < 1e-6f) { return; }
            out.push_back({ segment, kind });
        }

        void addLineSegment(std::vector<SliceSegment>& out, const Pos& a, const Pos& b, BoundaryKind kind = BoundaryKind::Air) {
            addSegment(out, Segment::Line(a, b), kind);
        }

        bool addOccLineSegment(std::vector<SliceSegment>& out, BRepAdaptor_Curve& curve, double first, double last) {
            Pos a = posFromGp(curve.Value(first));
            Pos b = posFromGp(curve.Value(last));

            addLineSegment(out, a, b);

            return true;
        }

        void addArcRange(std::vector<SliceSegment>& out, BRepAdaptor_Curve& curve, const Pos& center, double first, double last) {
            double mid = first + (last - first) * 0.5;

            Pos a = posFromGp(curve.Value(first));
            Pos b = posFromGp(curve.Value(mid));
            Pos c = posFromGp(curve.Value(last));

            addSegment(out, Segment::Arc(a, b, c, center));
        }

        bool addOccCircleSegments(std::vector<SliceSegment>& out, BRepAdaptor_Curve& curve, double first, double last) {
            gp_Circ circle = curve.Circle();
            Pos center = posFromGp(circle.Location());

            double span = last - first;

            if (span <= 1e-9) { return false; }

            double mid = first + span * 0.5;

            addArcRange(out, curve, center, first, mid);
            addArcRange(out, curve, center, mid, last);

            return true;
        }

        bool addAnalyticCurveSegment(std::vector<SliceSegment>& out, BRepAdaptor_Curve& curve, double first, double last) {
            switch (curve.GetType()) {
                case GeomAbs_Line: return addOccLineSegment(out, curve, first, last);
                case GeomAbs_Circle: return addOccCircleSegments(out, curve, first, last);
                default: return false;
            }

            return false;
        }

        void addSampledCurveSegments(std::vector<SliceSegment>& out, BRepAdaptor_Curve& curve, double first, double last) {
            std::vector<Pos> sampled;

            double lengthStep = 0.25;
            GCPnts_UniformAbscissa sampler(curve, lengthStep, first, last);

            if (sampler.IsDone() && sampler.NbPoints() >= 2) {
                for (int i = 1; i <= sampler.NbPoints(); i++) {
                    gp_Pnt p = curve.Value(sampler.Parameter(i));
                    sampled.push_back(posFromGp(p));
                }
            }

            else {
                int samples = 12;

                for (int i = 0; i <= samples; i++) {
                    double u = first + (last - first) * (double(i) / double(samples));
                    gp_Pnt p = curve.Value(u);
                    sampled.push_back(posFromGp(p));
                }
            }

            for (size_t i = 0; i + 1 < sampled.size(); i++) {
                addLineSegment(out, sampled[i], sampled[i + 1]);
            }

            if (curve.IsClosed() && sampled.size() >= 2) {
                addLineSegment(out, sampled.back(), sampled.front());
            }
        }

        bool sampleSectionSegments(const Model& model, std::vector<SliceSegment>& out) {
            out.clear();

            if (!model.loaded) { return false; }
            if (model.shape.IsNull()) { return false; }

            gp_Pln plane(gp_Pnt(0.0, 0.0, double(z)), gp_Dir(0.0, 0.0, 1.0));

            BRepAlgoAPI_Section section(model.shape, plane, false);

            section.ComputePCurveOn1(true);
            section.Approximation(true);
            section.Build();

            if (!section.IsDone()) { return false; }

            TopoDS_Shape sectionShape = section.Shape();

            if (sectionShape.IsNull()) { return false; }

            size_t edgeCount = 0;
            size_t segmentCount = 0;
            size_t lineCount = 0;
            size_t circleCount = 0;
            size_t sampledCount = 0;

            for (TopExp_Explorer exp(sectionShape, TopAbs_EDGE); exp.More(); exp.Next()) {
                edgeCount += 1;

                TopoDS_Edge edge = TopoDS::Edge(exp.Current());
                BRepAdaptor_Curve curve(edge);

                double first = curve.FirstParameter();
                double last = curve.LastParameter();

                if (last <= first) { continue; }

                size_t before = out.size();
                GeomAbs_CurveType type = curve.GetType();

                if (addAnalyticCurveSegment(out, curve, first, last)) {
                    if (type == GeomAbs_Line) { lineCount += out.size() - before; }
                    if (type == GeomAbs_Circle) { circleCount += out.size() - before; }
                }

                else {
                    addSampledCurveSegments(out, curve, first, last);
                    sampledCount += out.size() - before;
                }

                segmentCount += out.size() - before;
            }

            dbg(
                "[ToolPathSlice] z=%.3f sectionEdges=%zu sectionSegments=%zu lineSegments=%zu circleSegments=%zu sampledSegments=%zu",
                z,
                edgeCount,
                segmentCount,
                lineCount,
                circleCount,
                sampledCount
            );

            return !out.empty();
        }

        bool computeContours(const Model& toCarve, const Model& toAvoid) {
            clear();

            if (!sampleSectionSegments(toAvoid, avoid)) {
                avoid.clear();
            }

            std::vector<SliceSegment> rawCarve;

            if (!sampleSectionSegments(toCarve, rawCarve)) {
                return false;
            }

            for (const SliceSegment& s : rawCarve) {
                BoundaryKind kind = segmentNearAvoid(s.segment) ? BoundaryKind::Part : BoundaryKind::Air;

                carve.push_back({ s.segment, kind });
                includeSegment(s.segment);
            }

            return valid && !carve.empty();
        }

        // Hatch
        //--------------------------------------------------

        bool lineCrossingAtY(const Segment& segment, float y, float& x) const {
            const Pos& a = segment.a;
            const Pos& b = segment.b;

            if (std::abs(a.y - b.y) < 1e-6f) { return false; }

            float yMin = std::min(a.y, b.y);
            float yMax = std::max(a.y, b.y);

            if (y < yMin || y >= yMax) { return false; }

            float t = (y - a.y) / (b.y - a.y);
            x = a.x + (b.x - a.x) * t;

            return true;
        }

        void collectSegmentCrossingsAtY(const Segment& segment, float y, BoundaryKind kind, std::vector<SliceCrossing>& crossings, int samples = 24) const {
            if (segment.kind == Segment::Type::Line) {
                float x = 0.0f;
                if (lineCrossingAtY(segment, y, x)) { crossings.push_back({ x, kind }); }
                return;
            }

            for (int i = 0; i < samples; i++) {
                Segment line = Segment::Line(
                    segment.at(float(i) / float(samples)),
                    segment.at(float(i + 1) / float(samples))
                );

                float x = 0.0f;
                if (lineCrossingAtY(line, y, x)) { crossings.push_back({ x, kind }); }
            }
        }

        void collectCrossingsAtY(float y, std::vector<SliceCrossing>& crossings) {
            crossings.clear();

            for (const SliceSegment& s : carve) {
                collectSegmentCrossingsAtY(s.segment, y, s.kind, crossings);
            }

            std::sort(
                crossings.begin(),
                crossings.end(),
                [](const SliceCrossing& a, const SliceCrossing& b) { return a.x < b.x; }
            );

            std::vector<SliceCrossing> unique;
            float eps = 1e-4f;

            for (SliceCrossing c : crossings) {
                if (!unique.empty() && std::abs(unique.back().x - c.x) < eps) {
                    if (c.kind == BoundaryKind::Part) { unique.back().kind = BoundaryKind::Part; }
                    continue;
                }

                unique.push_back(c);
            }

            crossings = unique;
        }

        void solveHatch(const Tool& tool, bool flipHatchDirection = false, bool airCut = false, float airExtension = 0.0f) {
            points.clear();

            if (!valid) { return; }
            if (carve.empty()) { return; }

            float spacing = static_cast<float>(tool.diameter);
            float radius = static_cast<float>(tool.radius);

            if (spacing <= 0.0f) { spacing = 1.0f; }
            if (airExtension < 0.0f) { airExtension = 0.0f; }

            float y0 = min.y + radius;
            float y1 = max.y - radius;

            if (y1 < y0) { return; }

            std::vector<SliceCrossing> crossings;

            size_t row = 0;
            size_t segments = 0;

            for (float y = y0; y <= y1 + 1e-4f; y += spacing) {
                collectCrossingsAtY(y, crossings);

                if (crossings.size() < 2) {
                    row += 1;
                    continue;
                }

                bool leftToRight = ((row % 2) == 0);
                if (flipHatchDirection) { leftToRight = !leftToRight; }

                for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
                    SliceCrossing left = crossings[i];
                    SliceCrossing right = crossings[i + 1];

                    float x0 = left.x;
                    float x1 = right.x;

                    if (left.kind == BoundaryKind::Part) { x0 += radius; }
                    else if (airCut) { x0 -= airExtension; }

                    if (right.kind == BoundaryKind::Part) { x1 -= radius; }
                    else if (airCut) { x1 += airExtension; }

                    if (x1 < x0) { continue; }

                    if (leftToRight) {
                        points.push_back({ x0, y });
                        points.push_back({ x1, y });
                    }

                    else {
                        points.push_back({ x1, y });
                        points.push_back({ x0, y });
                    }

                    segments += 1;
                }

                row += 1;
            }

            dbg(
                "[ToolPathSlice] z=%.3f carveSegments=%zu avoidSegments=%zu hatchSegments=%zu hatchPoints=%zu airCut=%i airExtension=%.3f",
                z,
                carve.size(),
                avoid.size(),
                segments,
                points.size(),
                int(airCut),
                airExtension
            );
        }
    };

    struct ToolPath {
        Tool tool = Tool::GodTool();

        std::vector<ToolPathSlice> slices;
        std::vector<ToolPathPoint> points;

        bool computed = false;

        double stepDown = 1.0;

        bool airCut = true;
        float airExtension = 5.0f;

        void clear() {
            slices.clear();
            points.clear();
            computed = false;
        }

        bool empty() const { return points.empty(); }
        size_t size() const { return points.size(); }

        bool boundsFromModel(const Model& model, Pos3& min, Pos3& max) {
            if (!model.loaded) { return false; }
            if (model.render.triangles.empty()) { return false; }

            bool valid = false;

            for (const Rev::Core::Vertex3& v : model.render.triangles) {
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

        void buildPointsFromSlices() {
            points.clear();

            double t = 0.0;

            for (const ToolPathSlice& slice : slices) {
                for (const Pos& p : slice.points) {
                    points.push_back({
                        .position = { p.x, p.y, slice.z },
                        .t = t,
                        .rapid = false,
                        .cutting = true
                    });

                    t += 1.0;
                }
            }
        }

        bool compute(Model& toCarve, Model& toAvoid, Tool& tool) {
            clear();

            this->tool = tool;

            dbg("[ToolPath] Computing toolpath");

            Pos3 min;
            Pos3 max;

            if (!boundsFromModel(toCarve, min, max)) {
                dbg("[ToolPath] Failed: no carve bounds");
                return false;
            }

            dbg(
                "[ToolPath] Carve bounds min=(%.3f %.3f %.3f), max=(%.3f %.3f %.3f)",
                min.x, min.y, min.z,
                max.x, max.y, max.z
            );

            float dz = static_cast<float>(stepDown);

            if (dz <= 0.0f) { dz = 1.0f; }

            bool flipHatchDirection = false;

            size_t attempted = 0;
            size_t contoured = 0;
            size_t solved = 0;

            for (float z = min.z; z <= max.z + 1e-4f; z += dz) {
                attempted += 1;

                ToolPathSlice slice;
                slice.z = z;

                if (!slice.computeContours(toCarve, toAvoid)) {
                    dbg("[ToolPath] z=%.3f: no carve contour", z);
                    continue;
                }

                contoured += 1;

                slice.solveHatch(this->tool, flipHatchDirection, airCut, airExtension);

                if (!slice.points.empty()) {
                    slices.push_back(slice);
                    solved += 1;
                    flipHatchDirection = !flipHatchDirection;
                }
            }

            buildPointsFromSlices();

            computed = !points.empty();

            dbg(
                "[ToolPath] Done. attempted=%zu contoured=%zu solved=%zu slices=%zu points=%zu computed=%i",
                attempted,
                contoured,
                solved,
                slices.size(),
                points.size(),
                int(computed)
            );

            return computed;
        }

        bool compute(Model& toCarve, Model& toAvoid) {
            Tool tool = Tool::GodTool();
            return compute(toCarve, toAvoid, tool);
        }

        bool computeFromDelta(Model& deltaModel, Model& remainingModel, Tool tool = Tool::GodTool()) {
            return compute(deltaModel, remainingModel, tool);
        }

        void buildLineSegments(std::vector<Rev::Core::Vertex3>& lines) const {
            lines.clear();

            if (points.size() < 2) { return; }

            Rev::Core::Color cutColor = { 1.0f, 0.0f, 1.0f, 1.0f };
            Rev::Core::Color rapidColor = { 0.6f, 0.0f, 1.0f, 0.35f };

            for (size_t i = 0; i + 1 < points.size(); i++) {
                const Pos3& a = points[i].position;
                const Pos3& b = points[i + 1].position;

                Rev::Core::Color color = points[i + 1].rapid ? rapidColor : cutColor;

                lines.push_back({ a.x, a.y, a.z, color });
                lines.push_back({ b.x, b.y, b.z, color });
            }
        }
    };
}