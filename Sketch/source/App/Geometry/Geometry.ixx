module;

#include <vector>
#include <cmath>

#include <nlohmann/json.hpp>

export module Sketch.App.Geometry;

import Rev.Core.Pos;

export namespace Sketch::App {

    using Json = nlohmann::json;
    using Rev::Core::Pos;

    // A JSON-serialisable 2D position: Rev's Pos plus a clean
    // round-trip to { "x", "y" }. Used as the stored point type for every sketch
    // primitive, so geometry math gets Pos's vector algebra (b - a, normalize,
    // dot, cross, angleTo, ...) for free.
    struct Pos2 : public Pos {

        using Pos::Pos;                                   // inherit (x,y) constructors
        Pos2() = default;
        Pos2(const Pos& p) : Pos(p) {}

        Json toJson() const { return Json{ { "x", x }, { "y", y } }; }

        static Pos2 fromJson(const Json& j) {
            return Pos2(j.value("x", 0.0), j.value("y", 0.0));
        }
    };

    // The basic 2D sketch primitives, all in world (sketch) coordinates.
    // Compound shapes are built from these later.

    // A point is a position with a construction flag.
    struct Point2 : public Pos2 {

        bool construction = false;   // reference geometry, not real output

        Point2() = default;
        Point2(float x, float y) : Pos2(x, y) {}
        Point2(const Pos& p) : Pos2(p) {}

        Json toJson() const { return Json{ { "x", x }, { "y", y }, { "construction", construction } }; }

        static Point2 fromJson(const Json& j) {
            Point2 p(j.value("x", 0.0), j.value("y", 0.0));
            p.construction = j.value("construction", false);
            return p;
        }
    };

    struct Segment2 {

        Pos2 a;   // start point
        Pos2 b;   // end point
        bool construction = false;

        Json toJson() const {
            return Json{ { "a", a.toJson() }, { "b", b.toJson() },
                         { "construction", construction } };
        }

        static Segment2 fromJson(const Json& j) {
            return {
                Pos2::fromJson(j.value("a", Json::object())),
                Pos2::fromJson(j.value("b", Json::object())),
                j.value("construction", false)
            };
        }
    };

    // A connected chain of points: one continuous path (the line tool's output).
    // A 2-point polyline is just a single segment; more points form a chain whose
    // interior joints are real, linked vertices.
    struct Polyline2 {

        std::vector<Point2> points;
        bool construction = false;

        Json toJson() const {
            Json arr = Json::array();
            for (const Point2& p : points) { arr.push_back(p.toJson()); }
            return Json{ { "points", arr }, { "construction", construction } };
        }

        static Polyline2 fromJson(const Json& j) {
            Polyline2 line;
            if (auto it = j.find("points"); it != j.end() && it->is_array()) {
                for (const Json& e : *it) { line.points.push_back(Point2::fromJson(e)); }
            }
            line.construction = j.value("construction", false);
            return line;
        }
    };

    struct Circle2 {

        Pos2 c;          // center
        float r = 0.0f;  // radius
        bool construction = false;

        Json toJson() const {
            return Json{ { "c", c.toJson() }, { "r", r }, { "construction", construction } };
        }

        static Circle2 fromJson(const Json& j) {
            return { Pos2::fromJson(j.value("c", Json::object())),
                     j.value("r", 0.0f), j.value("construction", false) };
        }
    };

    // Arc defined by a center and two endpoints A and B (both on the circle, which
    // fixes the radius). There is no stored chirality: the arc is *always* the one
    // swept counterclockwise from A to B. The two possible arcs through A and B are
    // distinguished purely by the order of the endpoints (swapping A and B selects
    // the complementary arc).
    struct Arc2 {

        Pos2 c;   // center
        Pos2 a;   // start point (CCW start)
        Pos2 b;   // end point   (CCW end)
        bool construction = false;

        Json toJson() const {
            return Json{ { "c", c.toJson() }, { "a", a.toJson() }, { "b", b.toJson() },
                         { "construction", construction } };
        }

        static Arc2 fromJson(const Json& j) {
            return {
                Pos2::fromJson(j.value("c", Json::object())),
                Pos2::fromJson(j.value("a", Json::object())),
                Pos2::fromJson(j.value("b", Json::object())),
                j.value("construction", false)
            };
        }
    };

    // An ellipse, stored the way it is actually defined: a centre C and two
    // conjugate semi-axis vectors U and V. No angle is stored -- the orientation
    // lives in the components of U and V. This is the form that drops directly into
    // closed-form intersection: substituting the parametric point into a line or
    // conic gives a quadratic in (cos t, sin t).
    //
    //   P(t) = C + cos(t) * U + sin(t) * V
    //
    // The tool builds it point-by-point: centre, a major-axis point A (U = A - C),
    // and a perimeter point B (V = the conjugate semi-axis B implies).
    struct Ellipse2 {

        Pos2 c;   // centre C
        Pos2 u;   // semi-axis vector U (P(0) = C + U)
        Pos2 v;   // conjugate semi-axis vector V
        bool construction = false;

        Json toJson() const {
            return Json{ { "c", c.toJson() }, { "u", u.toJson() }, { "v", v.toJson() },
                         { "construction", construction } };
        }

        static Ellipse2 fromJson(const Json& j) {
            return { Pos2::fromJson(j.value("c", Json::object())),
                     Pos2::fromJson(j.value("u", Json::object())),
                     Pos2::fromJson(j.value("v", Json::object())),
                     j.value("construction", false) };
        }
    };

    // An elliptical arc: an ellipse (centre + conjugate semi-axes U, V) plus start
    // and end parameters. Like the circular arc there is no stored chirality: the
    // arc is always swept by *increasing* parameter from a0 to a1, which is
    // counterclockwise because the tool always builds a right-handed (U, V) frame.
    // Swapping a0 and a1 selects the complementary arc.
    struct EllipseArc2 {

        Pos2 c;
        Pos2 u;
        Pos2 v;
        float a0 = 0.0f, a1 = 0.0f;   // start / end parameter (swept a0 -> a1 CCW)
        bool construction = false;

        Json toJson() const {
            return Json{
                { "c", c.toJson() }, { "u", u.toJson() }, { "v", v.toJson() },
                { "a0", a0 }, { "a1", a1 },
                { "construction", construction }
            };
        }

        static EllipseArc2 fromJson(const Json& j) {
            return {
                Pos2::fromJson(j.value("c", Json::object())),
                Pos2::fromJson(j.value("u", Json::object())),
                Pos2::fromJson(j.value("v", Json::object())),
                j.value("a0", 0.0f), j.value("a1", 0.0f),
                j.value("construction", false)
            };
        }
    };

    // Ellipse math (shared by the tools and the view)
    //--------------------------------------------------

    // World point at parameter t: P(t) = C + cos(t)*U + sin(t)*V.
    inline Pos ellipsePointAt(const Pos& c, const Pos& u, const Pos& v, float t) {
        return c + u * std::cos(t) + v * std::sin(t);
    }

    // Parameter t of a world point. With orthogonal conjugate axes (as the tool
    // always produces), cos t and sin t are just the projections onto U and V
    // normalised by their squared lengths.
    inline float ellipseParamOf(const Pos& c, const Pos& u, const Pos& v, const Pos& w) {
        Pos d = w - c;
        float lu = u.dot(u);
        float lv = v.dot(v);
        float cs = (lu > 1e-12f) ? d.dot(u) / lu : 0.0f;
        float sn = (lv > 1e-12f) ? d.dot(v) / lv : 0.0f;
        return std::atan2(sn, cs);
    }

    // Fit the unique ellipse from a centre, a major-axis point A (on the perimeter,
    // fixing U = A - C) and a second perimeter point B (fixing the conjugate
    // semi-axis V, perpendicular to U). B need not lie on the minor axis.
    struct EllipseFit { Pos u, v; };

    inline EllipseFit fitEllipse(const Pos& c, const Pos& a, const Pos& b) {
        EllipseFit f;
        f.u = a - c;

        float al = f.u.pythag();
        if (al < 1e-6f) { return f; }

        Pos m    = f.u / al;   // unit major axis
        Pos perp = m.normal(); // CCW perpendicular (minor-axis direction)

        Pos d = b - c;
        float p = d.dot(m);     // B's coordinate along the major axis
        float q = d.dot(perp);  // B's coordinate along the minor axis

        float k = 1.0f - (p / al) * (p / al);
        float bb = (k > 1e-6f) ? std::fabs(q) / std::sqrt(k) : std::fabs(q);

        f.v = perp * bb;
        return f;
    }

    // The full contents of a sketch: one bucket per primitive kind. Used both as
    // the project's committed geometry and as a tool's transient preview.
    struct SketchGeometry {

        std::vector<Point2> points;
        std::vector<Segment2> segments;
        std::vector<Polyline2> polylines;
        std::vector<Circle2> circles;
        std::vector<Arc2> arcs;
        std::vector<Ellipse2> ellipses;
        std::vector<EllipseArc2> ellipseArcs;

        void clear() {
            points.clear();
            segments.clear();
            polylines.clear();
            circles.clear();
            arcs.clear();
            ellipses.clear();
            ellipseArcs.clear();
        }

        bool empty() const {
            return points.empty() && segments.empty() && polylines.empty()
                && circles.empty() && arcs.empty()
                && ellipses.empty() && ellipseArcs.empty();
        }

        Json toJson() const {

            Json out;

            out["points"]      = Json::array();
            out["segments"]    = Json::array();
            out["polylines"]   = Json::array();
            out["circles"]     = Json::array();
            out["arcs"]        = Json::array();
            out["ellipses"]    = Json::array();
            out["ellipseArcs"] = Json::array();

            for (const Point2& p : points)        { out["points"].push_back(p.toJson()); }
            for (const Segment2& s : segments)    { out["segments"].push_back(s.toJson()); }
            for (const Polyline2& l : polylines)  { out["polylines"].push_back(l.toJson()); }
            for (const Circle2& c : circles)      { out["circles"].push_back(c.toJson()); }
            for (const Arc2& a : arcs)            { out["arcs"].push_back(a.toJson()); }
            for (const Ellipse2& e : ellipses)    { out["ellipses"].push_back(e.toJson()); }
            for (const EllipseArc2& e : ellipseArcs) { out["ellipseArcs"].push_back(e.toJson()); }

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
            if (auto it = j.find("ellipses"); it != j.end() && it->is_array()) {
                for (const Json& e : *it) { g.ellipses.push_back(Ellipse2::fromJson(e)); }
            }
            if (auto it = j.find("ellipseArcs"); it != j.end() && it->is_array()) {
                for (const Json& e : *it) { g.ellipseArcs.push_back(EllipseArc2::fromJson(e)); }
            }

            return g;
        }
    };
}
