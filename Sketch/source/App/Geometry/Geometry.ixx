module;

#include <vector>

#include <nlohmann/json.hpp>

export module Sketch.App.Geometry;

export namespace Sketch::App {

    using Json = nlohmann::json;

    // The basic 2D sketch primitives, all in world (sketch) coordinates.
    // Compound shapes are built from these later.

    struct Point2 {

        double x = 0.0, y = 0.0;

        Json toJson() const { return Json{ { "x", x }, { "y", y } }; }

        static Point2 fromJson(const Json& j) {
            return { j.value("x", 0.0), j.value("y", 0.0) };
        }
    };

    struct Segment2 {

        double ax = 0.0, ay = 0.0;   // start point
        double bx = 0.0, by = 0.0;   // end point

        Json toJson() const {
            return Json{ { "ax", ax }, { "ay", ay }, { "bx", bx }, { "by", by } };
        }

        static Segment2 fromJson(const Json& j) {
            return {
                j.value("ax", 0.0), j.value("ay", 0.0),
                j.value("bx", 0.0), j.value("by", 0.0)
            };
        }
    };

    // A connected chain of points: one continuous path (the line tool's output).
    // A 2-point polyline is just a single segment; more points form a chain whose
    // interior joints are real, linked vertices.
    struct Polyline2 {

        std::vector<Point2> points;

        Json toJson() const {
            Json arr = Json::array();
            for (const Point2& p : points) { arr.push_back(p.toJson()); }
            return Json{ { "points", arr } };
        }

        static Polyline2 fromJson(const Json& j) {
            Polyline2 line;
            if (auto it = j.find("points"); it != j.end() && it->is_array()) {
                for (const Json& e : *it) { line.points.push_back(Point2::fromJson(e)); }
            }
            return line;
        }
    };

    struct Circle2 {

        double cx = 0.0, cy = 0.0;   // center
        double r = 0.0;              // radius

        Json toJson() const {
            return Json{ { "cx", cx }, { "cy", cy }, { "r", r } };
        }

        static Circle2 fromJson(const Json& j) {
            return { j.value("cx", 0.0), j.value("cy", 0.0), j.value("r", 0.0) };
        }
    };

    // Arc defined by a center, a start point A and end point B (both on the
    // circle, fixing the radius), plus a point D somewhere on the arc between
    // them. D is what makes the arc unambiguous: rather than storing a sweep
    // direction, chirality is *implicit* in which side of A->B the midpoint D
    // sits, so the arc is simply the one through A, D, B.
    struct Arc2 {

        double cx = 0.0, cy = 0.0;   // center
        double ax = 0.0, ay = 0.0;   // start point
        double bx = 0.0, by = 0.0;   // end point
        double dx = 0.0, dy = 0.0;   // a point on the arc (mid), encodes chirality

        Json toJson() const {
            return Json{
                { "cx", cx }, { "cy", cy },
                { "ax", ax }, { "ay", ay },
                { "bx", bx }, { "by", by },
                { "dx", dx }, { "dy", dy }
            };
        }

        static Arc2 fromJson(const Json& j) {
            return {
                j.value("cx", 0.0), j.value("cy", 0.0),
                j.value("ax", 0.0), j.value("ay", 0.0),
                j.value("bx", 0.0), j.value("by", 0.0),
                j.value("dx", 0.0), j.value("dy", 0.0)
            };
        }
    };

    // The full contents of a sketch: one bucket per primitive kind. Used both as
    // the project's committed geometry and as a tool's transient preview.
    struct SketchGeometry {

        std::vector<Point2> points;
        std::vector<Segment2> segments;
        std::vector<Polyline2> polylines;
        std::vector<Circle2> circles;
        std::vector<Arc2> arcs;

        void clear() {
            points.clear();
            segments.clear();
            polylines.clear();
            circles.clear();
            arcs.clear();
        }

        bool empty() const {
            return points.empty() && segments.empty() && polylines.empty()
                && circles.empty() && arcs.empty();
        }

        Json toJson() const {

            Json out;

            out["points"]    = Json::array();
            out["segments"]  = Json::array();
            out["polylines"] = Json::array();
            out["circles"]   = Json::array();
            out["arcs"]      = Json::array();

            for (const Point2& p : points)      { out["points"].push_back(p.toJson()); }
            for (const Segment2& s : segments)  { out["segments"].push_back(s.toJson()); }
            for (const Polyline2& l : polylines) { out["polylines"].push_back(l.toJson()); }
            for (const Circle2& c : circles)    { out["circles"].push_back(c.toJson()); }
            for (const Arc2& a : arcs)          { out["arcs"].push_back(a.toJson()); }

            return out;
        }

        static SketchGeometry fromJson(const Json& j) {

            SketchGeometry g;

            if (auto it = j.find("points"); it != j.end() && it->is_array()) {
                for (const Json& e : *it) { g.points.push_back(Point2::fromJson(e)); }
            }
            if (auto it = j.find("segments"); it != j.end() && it->is_array()) {
                for (const Json& e : *it) { g.segments.push_back(Segment2::fromJson(e)); }
            }
            if (auto it = j.find("polylines"); it != j.end() && it->is_array()) {
                for (const Json& e : *it) { g.polylines.push_back(Polyline2::fromJson(e)); }
            }
            if (auto it = j.find("circles"); it != j.end() && it->is_array()) {
                for (const Json& e : *it) { g.circles.push_back(Circle2::fromJson(e)); }
            }
            if (auto it = j.find("arcs"); it != j.end() && it->is_array()) {
                for (const Json& e : *it) { g.arcs.push_back(Arc2::fromJson(e)); }
            }

            return g;
        }
    };
}
