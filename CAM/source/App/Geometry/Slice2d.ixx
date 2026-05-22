module;

#include <vector>
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

export module Cam.App.Slice2d;

import Rev.Core.Pos;

import Cam.App.Model;
import Cam.App.Tool;

import Cam.App.Slicer.Strategy.Slice.Segment2;
import Cam.App.Slicer.Strategy.Slice.Chain;
import Cam.App.Slicer.Strategy.Slice.Profile;

export namespace Cam::App {

    using namespace Rev::Core;
    using namespace Slicer::Strategy::Slice;

    struct Slice2d {

        enum class Strategy {
            Hatch,
            Profile
        };

        static std::string strategyToString(Strategy strategy) {

            switch (strategy) {

                case Strategy::Hatch:
                    return "Hatch";

                case Strategy::Profile:
                    return "Profile";
            }

            return "Profile";
        }

        static Strategy strategyFromString(const std::string& value) {

            if (value == "Hatch") {
                return Strategy::Hatch;
            }

            return Strategy::Profile;
        }

        static std::string strategyDisplayName(Strategy strategy) {

            switch (strategy) {

                case Strategy::Hatch:
                    return "Hatch";

                case Strategy::Profile:
                    return "Profile";
            }

            return "Profile";
        }

        float z = 0.0f;

        Pos min = {};
        Pos max = {};
        bool valid = false;

        Strategy strategy = Strategy::Profile;
        Tool tool = Tool::GodTool();

        std::vector<Segment> source;
        std::vector<Profile> profiles;
        std::vector<Segment> paths;
        std::vector<Pos> points;

        // State
        //--------------------------------------------------

        void clear() {

            source.clear();
            profiles.clear();
            paths.clear();
            points.clear();

            min = {};
            max = {};
            valid = false;
        }

        bool empty() const {
            return source.empty() && paths.empty() && points.empty();
        }

        bool hasPointPath() const {
            return !points.empty();
        }

        // Bounds
        //--------------------------------------------------

        void includePoint(const Pos& p) {

            if (!p) { return; }

            if (!valid) {
                min = p;
                max = p;
                valid = true;
                return;
            }

            min = Pos::min(min, p);
            max = Pos::max(max, p);
        }

        void includeSegment(
            const Segment& s,
            int samples = 24
        ) {
            if (s.kind == Segment::Kind::Line) {
                includePoint(s.start());
                includePoint(s.end());
                return;
            }

            if (samples < 1) { samples = 1; }

            for (int i = 0; i <= samples; i++) {
                includePoint(
                    s.pointAt(float(i) / float(samples))
                );
            }
        }

        void addSegment(const Segment& s) {

            if (!s.valid()) { return; }

            source.push_back(s);
            includeSegment(s);
        }

        void addLine(
            const Pos& a,
            const Pos& b
        ) {
            addSegment(
                Segment::Line(a, b)
            );
        }

        void setSource(
            const std::vector<Segment>& segments
        ) {
            clear();

            for (const Segment& s : segments) {
                addSegment(s);
            }
        }

        static Pos posFromGp(const gp_Pnt& p) {

            return {
                static_cast<float>(p.X()),
                static_cast<float>(p.Y())
            };
        }

        // OCC extraction
        //--------------------------------------------------

        static bool addOccLine(
            Slice2d& slice,
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
            Slice2d& slice,
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

            // Split circles/arcs into two arcs for safer chain stitching
            // and bounds sampling.
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
            Slice2d& slice,
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
            Slice2d& slice,
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

        static Slice2d FromModel(
            const Model& model,
            float z,
            Strategy strategy,
            const Tool& tool
        ) {
            Slice2d slice;

            slice.z = z;
            slice.strategy = strategy;
            slice.tool = tool;

            if (!model.loaded) { return slice; }
            if (model.shape.IsNull()) { return slice; }

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

            if (!section.IsDone()) { return slice; }

            TopoDS_Shape sectionShape = section.Shape();

            if (sectionShape.IsNull()) { return slice; }

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
                "[Slice2d] z=%.3f edges=%zu segments=%zu lines=%zu circles=%zu sampled=%zu",
                z,
                edges,
                slice.source.size(),
                analyticLines,
                analyticCircles,
                sampledSegments
            );

            slice.build();

            return slice;
        }

        // Profile helpers
        //--------------------------------------------------

        Profile makeProfile() const {

            return Profile::FromSegments(
                source
            );
        }

        void collectProfile(
            const Profile& profile
        ) {
            for (const Profile::Entry& entry : profile.entries) {

                for (const Segment& s : entry.chain.segments) {
                    paths.push_back(s);
                }
            }
        }

        void buildInitialProfile() {

            profiles.clear();

            Profile profile = makeProfile();

            if (profile.empty()) {
                dbg(
                    "[Slice2d] z=%.3f profile empty source=%zu",
                    z,
                    source.size()
                );

                return;
            }

            profiles.push_back(profile);

            dbg(
                "[Slice2d] z=%.3f profile source=%zu entries=%zu outer=%zu holes=%zu open=%zu",
                z,
                source.size(),
                profile.size(),
                profile.outerCount(),
                profile.holeCount(),
                profile.openCount()
            );
        }

        // Strategies
        //--------------------------------------------------

        void build() {

            switch (strategy) {

                case Strategy::Hatch: {
                    buildHatch();
                    break;
                }

                case Strategy::Profile: {
                    buildProfile();
                    break;
                }
            }
        }

        void buildProfile() {

            paths.clear();
            points.clear();
            profiles.clear();

            Profile profile = makeProfile();

            if (profile.empty()) {

                dbg(
                    "[Slice2d] z=%.3f profile empty source=%zu",
                    z,
                    source.size()
                );

                return;
            }

            profiles.push_back(profile);

            float amount = static_cast<float>(
                tool.radius
            );

            dbg(
                "[Slice2d] z=%.3f profile start source=%zu entries=%zu outer=%zu holes=%zu radius=%.3f",
                z,
                source.size(),
                profile.size(),
                profile.outerCount(),
                profile.holeCount(),
                amount
            );

            collectProfile(profile);

            // Temporary rough spiral/profile test.
            //
            // Profile::inset() means "toward material":
            // outer loops inset, hole loops outset.
            for (int i = 0; i < 24; i++) {

                Profile next = profile.inset(amount);

                profiles.push_back(next);
                collectProfile(next);

                dbg(
                    "[Slice2d] z=%.3f profile inset=%i entries=%zu outer=%zu holes=%zu paths=%zu",
                    z,
                    i + 1,
                    next.size(),
                    next.outerCount(),
                    next.holeCount(),
                    paths.size()
                );

                profile = next;
            }

            dbg(
                "[Slice2d] z=%.3f profile done profiles=%zu paths=%zu",
                z,
                profiles.size(),
                paths.size()
            );
        }

        void buildHatch() {

            points.clear();
            paths.clear();
            profiles.clear();

            Profile profile = makeProfile();

            if (!profile.empty()) {
                profiles.push_back(profile);
            }

            // Placeholder: keep source visible until hatch is migrated to Profile.
            paths = source;

            dbg(
                "[Slice2d] z=%.3f hatch placeholder source=%zu paths=%zu entries=%zu",
                z,
                source.size(),
                paths.size(),
                profile.size()
            );
        }
    };
}