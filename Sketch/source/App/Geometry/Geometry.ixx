module;

#include <vector>
#include <cmath>

#include <nlohmann/json.hpp>

export module Sketch.App.Geometry;

export namespace Sketch::App {

    using Json = nlohmann::json;

    // The basic 2D sketch primitives, all in world (sketch) coordinates.
    // Compound shapes are built from these later.

    struct Point2 {

        double x = 0.0, y = 0.0;
        bool construction = false;   // reference geometry, not real output

        Json toJson() const { return Json{ { "x", x }, { "y", y }, { "construction", construction } }; }

        static Point2 fromJson(const Json& j) {
            return { j.value("x", 0.0), j.value("y", 0.0), j.value("construction", false) };
        }
    };

    struct Segment2 {

        double ax = 0.0, ay = 0.0;   // start point
        double bx = 0.0, by = 0.0;   // end point
        bool construction = false;

        Json toJson() const {
            return Json{ { "ax", ax }, { "ay", ay }, { "bx", bx }, { "by", by },
                         { "construction", construction } };
        }

        static Segment2 fromJson(const Json& j) {
            return {
                j.value("ax", 0.0), j.value("ay", 0.0),
                j.value("bx", 0.0), j.value("by", 0.0),
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

        double cx = 0.0, cy = 0.0;   // center
        double r = 0.0;              // radius
        bool construction = false;

        Json toJson() const {
            return Json{ { "cx", cx }, { "cy", cy }, { "r", r }, { "construction", construction } };
        }

        static Circle2 fromJson(const Json& j) {
            return { j.value("cx", 0.0), j.value("cy", 0.0), j.value("r", 0.0),
                     j.value("construction", false) };
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
        bool construction = false;

        Json toJson() const {
            return Json{
                { "cx", cx }, { "cy", cy },
                { "ax", ax }, { "ay", ay },
                { "bx", bx }, { "by", by },
                { "dx", dx }, { "dy", dy },
                { "construction", construction }
            };
        }

        static Arc2 fromJson(const Json& j) {
            return {
                j.value("cx", 0.0), j.value("cy", 0.0),
                j.value("ax", 0.0), j.value("ay", 0.0),
                j.value("bx", 0.0), j.value("by", 0.0),
                j.value("dx", 0.0), j.value("dy", 0.0),
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

        double cx = 0.0, cy = 0.0;     // centre C
        double ux = 0.0, uy = 0.0;     // semi-axis vector U (P(0) = C + U)
        double vx = 0.0, vy = 0.0;     // conjugate semi-axis vector V
        bool construction = false;

        Json toJson() const {
            return Json{ { "cx", cx }, { "cy", cy },
                         { "ux", ux }, { "uy", uy }, { "vx", vx }, { "vy", vy },
                         { "construction", construction } };
        }

        static Ellipse2 fromJson(const Json& j) {
            return { j.value("cx", 0.0), j.value("cy", 0.0),
                     j.value("ux", 0.0), j.value("uy", 0.0),
                     j.value("vx", 0.0), j.value("vy", 0.0),
                     j.value("construction", false) };
        }
    };

    // An elliptical arc: an ellipse (centre + conjugate semi-axes U, V) plus start
    // / end / mid parameters of the parametrisation. As with the circular arc,
    // chirality is implicit in the mid parameter `ad` (the swept midpoint), so the
    // arc is the one through a0, ad, a1.
    struct EllipseArc2 {

        double cx = 0.0, cy = 0.0;
        double ux = 0.0, uy = 0.0;
        double vx = 0.0, vy = 0.0;
        double a0 = 0.0, a1 = 0.0, ad = 0.0;   // start / end / mid parameter
        bool construction = false;

        Json toJson() const {
            return Json{
                { "cx", cx }, { "cy", cy },
                { "ux", ux }, { "uy", uy }, { "vx", vx }, { "vy", vy },
                { "a0", a0 }, { "a1", a1 }, { "ad", ad },
                { "construction", construction }
            };
        }

        static EllipseArc2 fromJson(const Json& j) {
            return {
                j.value("cx", 0.0), j.value("cy", 0.0),
                j.value("ux", 0.0), j.value("uy", 0.0),
                j.value("vx", 0.0), j.value("vy", 0.0),
                j.value("a0", 0.0), j.value("a1", 0.0), j.value("ad", 0.0),
                j.value("construction", false)
            };
        }
    };

    // Ellipse math (shared by the tools and the view)
    //--------------------------------------------------

    // World point at parameter t: P(t) = C + cos(t)*U + sin(t)*V.
    inline void ellipsePointAt(double cx, double cy, double ux, double uy,
                               double vx, double vy, double t, double& x, double& y) {
        double c = std::cos(t), s = std::sin(t);
        x = cx + c * ux + s * vx;
        y = cy + c * uy + s * vy;
    }

    // Parameter t of a world point. With orthogonal conjugate axes (as the tool
    // always produces), cos t and sin t are just the projections onto U and V
    // normalised by their squared lengths.
    inline double ellipseParamOf(double cx, double cy, double ux, double uy,
                                 double vx, double vy, double wx, double wy) {
        double dx = wx - cx, dy = wy - cy;
        double lu = ux * ux + uy * uy;
        double lv = vx * vx + vy * vy;
        double c = (lu > 1e-18) ? (dx * ux + dy * uy) / lu : 0.0;
        double s = (lv > 1e-18) ? (dx * vx + dy * vy) / lv : 0.0;
        return std::atan2(s, c);
    }

    // Fit the unique ellipse from a centre, a major-axis point A (on the perimeter,
    // fixing U = A - C) and a second perimeter point B (fixing the conjugate
    // semi-axis V, perpendicular to U). B need not lie on the minor axis.
    struct EllipseFit { double ux = 0.0, uy = 0.0, vx = 0.0, vy = 0.0; };

    inline EllipseFit fitEllipse(double cx, double cy, double ax, double ay,
                                 double bx, double by) {
        EllipseFit f;
        f.ux = ax - cx;
        f.uy = ay - cy;

        double a = std::hypot(f.ux, f.uy);
        if (a < 1e-9) { return f; }

        // Unit major axis and its CCW perpendicular (the minor-axis direction).
        double mux = f.ux / a, muy = f.uy / a;
        double px = -muy, py = mux;

        double dx = bx - cx, dy = by - cy;
        double p = dx * mux + dy * muy;   // B's coordinate along the major axis
        double q = dx * px  + dy * py;    // B's coordinate along the minor axis

        double k = 1.0 - (p / a) * (p / a);
        double b = (k > 1e-9) ? std::fabs(q) / std::sqrt(k) : std::fabs(q);

        f.vx = px * b;
        f.vy = py * b;
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
