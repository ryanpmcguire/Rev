module;

#include <vector>
#include <memory>
#include <string>
#include <utility>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <functional>
#include <cmath>
#include <algorithm>

#include <nlohmann/json.hpp>

export module Sketch.App.Geometry;

import Rev.Core.Pos;

export namespace Sketch::App {

    using Json = nlohmann::json;
    using Rev::Core::Pos;

    inline constexpr float PI  = 3.14159265358979323846f;
    inline constexpr float TAU = 2.0f * PI;

    // Stable identity
    //--------------------------------------------------
    // Every entity and every relation carries a unique 64-bit id, persisted as a
    // 16-digit hex string. Ids are the durable handle relations reference by; the
    // pointer graph between them is a derived cache rebuilt from ids on load.

    using Id = std::uint64_t;

    // A fresh random id (never 0, since 0 means "unassigned").
    inline Id newId() {
        static std::mt19937_64 rng(std::random_device{}());
        Id v = 0;
        while (v == 0) { v = rng(); }
        return v;
    }

    inline std::string idToHex(Id id) {
        char buf[17];
        std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(id));
        return std::string(buf);
    }

    inline Id idFromHex(const std::string& s) {
        return s.empty() ? 0 : static_cast<Id>(std::strtoull(s.c_str(), nullptr, 16));
    }

    // A JSON-serialisable 2D position: Rev's Pos plus a clean round-trip to
    // { "x", "y" }. The stored point type for every geometric entity, so the
    // geometry gets Pos's vector algebra (b - a, normalize, dot, cross, ...).
    struct Pos2 : public Pos {

        using Pos::Pos;                                   // inherit (x,y) constructors
        Pos2() = default;
        Pos2(const Pos& p) : Pos(p) {}

        Json toJson() const { return Json{ { "x", x }, { "y", y } }; }

        static Pos2 fromJson(const Json& j) {
            return Pos2(j.value("x", 0.0f), j.value("y", 0.0f));
        }
    };

    // Ellipse math (shared by entities and tools)
    //--------------------------------------------------

    // World point at parameter t: P(t) = C + cos(t)*U + sin(t)*V.
    inline Pos ellipsePointAt(const Pos& c, const Pos& u, const Pos& v, float t) {
        return c + u * std::cos(t) + v * std::sin(t);
    }

    // The *true* offset (parallel-curve) point at parameter t: P(t) displaced by `d`
    // along its real outward unit normal. The normal is perpendicular to the tangent
    // dP/dt = -sin t U + cos t V (correct even for conjugate, non-orthogonal axes),
    // oriented outward (away from centre). `d` is signed: +grows, -insets. This is
    // analytically equidistant -- the exact offset curve, which is NOT an ellipse.
    inline Pos ellipseOffsetPoint(const Pos& c, const Pos& u, const Pos& v, float t, float d) {
        Pos p = ellipsePointAt(c, u, v, t);
        Pos tan = u * (-std::sin(t)) + v * std::cos(t);
        Pos n(-tan.y, tan.x);
        float nl = n.pythag();
        if (nl > 1e-9f) { n = n / nl; }
        if (n.dot(p - c) < 0.0f) { n = n * -1.0f; }   // outward (away from centre)
        return p + n * d;
    }

    // Parameter t of a world point. With orthogonal conjugate axes, cos t / sin t
    // are the projections onto U and V normalised by their squared lengths.
    inline float ellipseParamOf(const Pos& c, const Pos& u, const Pos& v, const Pos& w) {
        Pos d = w - c;
        float lu = u.dot(u), lv = v.dot(v);
        float cs = (lu > 1e-12f) ? d.dot(u) / lu : 0.0f;
        float sn = (lv > 1e-12f) ? d.dot(v) / lv : 0.0f;
        return std::atan2(sn, cs);
    }

    // Fit the unique ellipse from a centre, a major-axis point A (fixing U = A - C)
    // and a perimeter point B (fixing the conjugate semi-axis V, perpendicular to U).
    struct EllipseFit { Pos u, v; };

    inline EllipseFit fitEllipse(const Pos& c, const Pos& a, const Pos& b) {
        EllipseFit f;
        f.u = a - c;
        float al = f.u.pythag();
        if (al < 1e-6f) { return f; }
        Pos m = f.u / al, perp = m.normal();
        Pos d = b - c;
        float p = d.dot(m), q = d.dot(perp);
        float k = 1.0f - (p / al) * (p / al);
        float bb = (k > 1e-6f) ? std::fabs(q) / std::sqrt(k) : std::fabs(q);
        f.v = perp * bb;
        return f;
    }

    // Circumcentre of three points (the centre of the unique circle through them).
    // Falls back to the centroid when they are (near) collinear.
    inline Pos circumcentre(Pos p0, Pos p1, Pos p2) {
        float d = 2.0f * (p0.x * (p1.y - p2.y) + p1.x * (p2.y - p0.y) + p2.x * (p0.y - p1.y));
        if (std::fabs(d) < 1e-9f) { return (p0 + p1 + p2) / 3.0f; }
        float a2 = p0.dot(p0), b2 = p1.dot(p1), c2 = p2.dot(p2);
        float ux = (a2 * (p1.y - p2.y) + b2 * (p2.y - p0.y) + c2 * (p0.y - p1.y)) / d;
        float uy = (a2 * (p2.x - p1.x) + b2 * (p0.x - p2.x) + c2 * (p1.x - p0.x)) / d;
        return Pos(ux, uy);
    }

    // Wrap an angle delta into [0, TAU).
    inline float wrapTau(float x) { while (x < 0.0f) { x += TAU; } while (x >= TAU) { x -= TAU; } return x; }

    // Sampling helpers (write a polyline approximation of a curve into `out`).
    //--------------------------------------------------

    inline int spanSteps(float span) {
        return std::max(2, static_cast<int>(std::ceil(std::fabs(span) / (TAU / 128.0f))));
    }

    inline void sampleArc(std::vector<Pos>& out, Pos c, float r, float a0, float span, int steps) {
        for (int i = 0; i <= steps; i++) {
            float a = a0 + span * (static_cast<float>(i) / steps);
            out.push_back(c + Pos::fromAngle(a) * r);
        }
    }

    inline void sampleEllipse(std::vector<Pos>& out, Pos c, Pos u, Pos v, float t0, float span, int steps) {
        for (int i = 0; i <= steps; i++) {
            float t = t0 + span * (static_cast<float>(i) / steps);
            out.push_back(ellipsePointAt(c, u, v, t));
        }
    }

    inline Pos closestOnSegment(Pos p, Pos a, Pos b) {
        Pos d = b - a;
        float len2 = d.dot(d);
        float t = (len2 > 1e-9f) ? std::clamp((p - a).dot(d) / len2, 0.0f, 1.0f) : 0.0f;
        return a + d * t;
    }

    // Exact intersection of straight segments / circular arcs. These operate on the
    // *true* analytic forms only -- nothing is ever quantised. Each appends 0..2
    // exact crossing points to `out`. (A full circle is the arc with sweep == TAU.)

    inline bool arcContains(Pos x, Pos c, float a0, float sweep) {
        if (sweep >= TAU - 1e-4f) { return true; }
        return wrapTau((x - c).angle() - a0) <= sweep + 1e-4f;
    }

    inline void isectSegSeg(Pos p1, Pos p2, Pos p3, Pos p4, std::vector<Pos>& out) {
        Pos r = p2 - p1, s = p4 - p3;
        float rxs = r.cross(s);
        if (std::fabs(rxs) < 1e-12f) { return; }                    // parallel / degenerate
        Pos qp = p3 - p1;
        float t = qp.cross(s) / rxs, u = qp.cross(r) / rxs;
        if (t < -1e-4f || t > 1.0f + 1e-4f || u < -1e-4f || u > 1.0f + 1e-4f) { return; }
        out.push_back(p1 + r * t);
    }

    inline void isectSegArc(Pos p1, Pos p2, Pos c, float rad, float a0, float sweep, std::vector<Pos>& out) {
        Pos d = p2 - p1, f = p1 - c;
        float A = d.dot(d);
        if (A < 1e-12f) { return; }
        float B = 2.0f * f.dot(d);
        float C = f.dot(f) - rad * rad;
        float disc = B * B - 4.0f * A * C;
        if (disc < 0.0f) { return; }
        disc = std::sqrt(disc);
        float ts[2] = { (-B - disc) / (2.0f * A), (-B + disc) / (2.0f * A) };
        for (float t : ts) {
            if (t < -1e-4f || t > 1.0f + 1e-4f) { continue; }
            Pos x = p1 + d * t;                                     // exact point on the line *and* circle
            if (arcContains(x, c, a0, sweep)) { out.push_back(x); }
        }
    }

    inline void isectArcArc(Pos c1, float r1, float a1, float s1,
                            Pos c2, float r2, float a2, float s2, std::vector<Pos>& out) {
        Pos d = c2 - c1;
        float dd = d.pythag();
        if (dd < 1e-6f) { return; }                                 // concentric
        if (dd > r1 + r2 + 1e-4f) { return; }                       // too far apart
        if (dd < std::fabs(r1 - r2) - 1e-4f) { return; }            // one inside the other
        float a = (r1 * r1 - r2 * r2 + dd * dd) / (2.0f * dd);
        float h2 = r1 * r1 - a * a;
        float h = (h2 > 0.0f) ? std::sqrt(h2) : 0.0f;
        Pos mid = c1 + d / dd * a;
        Pos perp = Pos(-d.y, d.x) / dd;
        for (float sgn : { 1.0f, -1.0f }) {
            Pos x = mid + perp * (h * sgn);
            if (arcContains(x, c1, a1, s1) && arcContains(x, c2, a2, s2)) { out.push_back(x); }
        }
    }

    // A single possible place the cursor could snap to, with a score. The view
    // gathers these from every nearby entity (plus intersections) each frame and
    // simply picks the highest-scoring one within reach -- and can draw them.
    //
    // Priority, highest first: a defined *point* (endpoint, centre, midpoint,
    // quadrant, vertex) > an *intersection* of two curves > the perpendicular
    // *foot* on a single curve. The tier (axis 2 > real 1 > construction 0) is a
    // minor tiebreak within a category.
    struct SnapCandidate {
        enum Kind { Point, Intersection, OnCurve };
        Pos pos;
        Kind kind = Point;
        int score = 0;
    };

    inline int snapScore(SnapCandidate::Kind kind, int tier) {
        int base = (kind == SnapCandidate::Point) ? 300
                 : (kind == SnapCandidate::Intersection) ? 200 : 100;
        return base + tier;
    }

    // ===============================================================
    // Stoicheion (στοιχεῖον) -- the fundamental geometric element.
    //
    struct MotionField;   // the solver's working buffer (defined below)

    // Euclid's "Elements" is the Stoicheia; a single primitive is a Stoicheion.
    // Each concrete type stores its own data and knows how to: serialise itself,
    // draw itself as a polyline (tessellate), measure distance to a probe point,
    // expose its anchor (snap) points and its mutable control points, and project
    // a point onto its body. Selection / rendering / snapping / moving in the view
    // are then a single loop of virtual calls -- no per-type branching.
    // ===============================================================
    struct Stoicheion {

        Id id = 0;                   // stable unique identity (assigned on commit)
        bool construction = false;   // reference geometry, not real output
        bool locked = false;         // datum (origin / axes): can't move or delete

        virtual ~Stoicheion() = default;

        // Identity / serialisation
        virtual const char* kind() const = 0;
        virtual std::unique_ptr<Stoicheion> clone() const = 0;
        virtual Json data() const = 0;          // type-specific payload
        virtual void load(const Json& j) = 0;   // read type-specific payload

        Json toJson() const {
            Json j = data();
            j["id"] = idToHex(id);
            j["kind"] = kind();
            j["construction"] = construction;
            j["locked"] = locked;
            return j;
        }

        // Rendering: append this entity's polyline approximation (world space).
        // Point-like entities append a single position, drawn as a dot.
        virtual void tessellate(std::vector<Pos>& out) const = 0;
        virtual bool isPoint() const { return false; }

        // Hit-testing: distance from p to the drawn curve.
        virtual float distanceTo(Pos p) const = 0;

        // Point-priority anchors (endpoints, centre) -- used for *selection*.
        virtual void anchors(std::vector<Pos>& out) const {}

        // Feature snap points: every salient position to magnetise placement onto
        // (endpoints, centres, midpoints, circle quadrants, ellipse axis-ends, ...).
        // Defaults to the selection anchors; entities override to add their extras.
        virtual void snapPoints(std::vector<Pos>& out) const { anchors(out); }

        // Exact-intersection forms, used to cross one entity with another. An
        // entity that is *genuinely* straight yields its segment(s); one that is
        // *genuinely* circular yields its circle/arc. Anything else (ellipses)
        // yields neither -- it produces no intersections rather than being
        // approximated. Nothing here is ever quantised.
        virtual void asLineSegments(std::vector<std::pair<Pos, Pos>>& out) const {}
        virtual bool asCircle(Pos& c, float& r, float& a0, float& sweep) const { return false; }

        // Yield this entity's own snap candidates for a cursor at `mouse`: every
        // feature point (scored Point) and the perpendicular foot (scored OnCurve).
        // Intersections are between *pairs* of entities, so the view adds those.
        virtual void snapCandidates(Pos mouse, std::vector<SnapCandidate>& out) const {
            int tier = locked ? 2 : (construction ? 0 : 1);
            std::vector<Pos> sps;
            snapPoints(sps);
            for (const Pos& p : sps) { out.push_back({ p, SnapCandidate::Point, snapScore(SnapCandidate::Point, tier) }); }
            Pos foot;
            if (footOnCurve(mouse, foot)) {
                out.push_back({ foot, SnapCandidate::OnCurve, snapScore(SnapCandidate::OnCurve, tier) });
            }
        }

        // Nearest point on the body for snapping onto the curve; false if none.
        virtual bool footOnCurve(Pos p, Pos& out) const { return false; }

        // Mutable control points -- every absolute point defining the entity (used
        // both for moving and for coincidence detection between entities).
        virtual void controlPoints(std::vector<Pos2*>& out) = 0;

        // Inherent invariants: the unbreakable relations *among an entity's own
        // points* (an arc's endpoints share the centre's radius, ...). The solver
        // runs this each pass so a control point can never drift off the shape it
        // defines. `id` is this entity's id, so it can name its own points; default:
        // a shape with no internal coupling (point, segment) does nothing.
        virtual void relaxInherent(MotionField& field, Id id, float rate) const {}

        // Offsetting. `offset` moves this curve sideways by `amount`, *in place*:
        // positive grows / outsets, negative insets (toward the curve's interior --
        // a smaller circle, a parallel line). `offsetBy` is the value-returning twin:
        // it leaves this one untouched and hands back a fresh offset copy. The pair
        // mirrors Pos::normalize / Pos::normalized. A directionless element (a point)
        // does not move. No chaining, no constraints -- each stoicheion offsets alone.
        virtual void offset(float amount) {}
        virtual std::unique_ptr<Stoicheion> offsetBy(float amount) const {
            std::unique_ptr<Stoicheion> copy = clone();
            copy->offset(amount);
            return copy;
        }

        // Offset into a (possibly multi-piece) result. A line / circle / arc offsets
        // exactly to one shape, so the default just emits offsetBy(). Curves whose
        // true offset is not the same kind of curve (ellipses) override this to emit
        // the analytically-equidistant offset as a polyline.
        virtual void offsetInto(float amount, std::vector<std::unique_ptr<Stoicheion>>& out) const {
            out.push_back(offsetBy(amount));
        }

        // Chirality marker housekeeping. A circle / arc carries its orientation as an
        // extra point (b / d). That point is NOT a solver variable and is NOT one of
        // the entity's "defining" (solvable) points -- it is just consistently stored
        // and re-seated to its canonical spot after any change, so it always reads at
        // the proper location and rides along when the entity moves.
        //   chiralitySlot : the anchor/control-point slot of that marker, or -1.
        //   normalize     : re-seat the marker canonically (preserving its side).
        virtual int chiralitySlot() const { return -1; }
        virtual void normalize() {}
    };

    // Exact mutual intersection of two entities. Uses only their true analytic
    // forms (straight segments / circular arcs); if either is neither (an ellipse),
    // no points are produced -- never an approximation.
    inline void intersect(const Stoicheion& A, const Stoicheion& B, std::vector<Pos>& out) {

        std::vector<std::pair<Pos, Pos>> as, bs;
        A.asLineSegments(as);
        B.asLineSegments(bs);

        Pos ac, bc; float ar, br, aa0, asw, ba0, bsw;
        bool aCirc = A.asCircle(ac, ar, aa0, asw);
        bool bCirc = B.asCircle(bc, br, ba0, bsw);

        for (const auto& sa : as) { for (const auto& sb : bs) { isectSegSeg(sa.first, sa.second, sb.first, sb.second, out); } }
        if (bCirc) { for (const auto& sa : as) { isectSegArc(sa.first, sa.second, bc, br, ba0, bsw, out); } }
        if (aCirc) { for (const auto& sb : bs) { isectSegArc(sb.first, sb.second, ac, ar, aa0, asw, out); } }
        if (aCirc && bCirc) { isectArcArc(ac, ar, aa0, asw, bc, br, ba0, bsw, out); }
    }

    // Point
    //--------------------------------------------------
    struct Point2 : public Stoicheion {

        Pos2 p;

        Point2() = default;
        Point2(const Pos& pos) : p(pos) {}

        const char* kind() const override { return "point"; }
        std::unique_ptr<Stoicheion> clone() const override { return std::make_unique<Point2>(*this); }
        Json data() const override { return Json{ { "p", p.toJson() } }; }
        void load(const Json& j) override { p = Pos2::fromJson(j.value("p", Json::object())); }

        void tessellate(std::vector<Pos>& out) const override { out.push_back(p); }
        bool isPoint() const override { return true; }
        float distanceTo(Pos q) const override { return (q - p).pythag(); }
        void anchors(std::vector<Pos>& out) const override { out.push_back(p); }
        void controlPoints(std::vector<Pos2*>& out) override { out.push_back(&p); }
    };

    // Segment
    //--------------------------------------------------
    struct Segment2 : public Stoicheion {

        Pos2 a, b;

        Segment2() = default;
        Segment2(const Pos& a, const Pos& b) : a(a), b(b) {}

        const char* kind() const override { return "segment"; }
        std::unique_ptr<Stoicheion> clone() const override { return std::make_unique<Segment2>(*this); }
        Json data() const override { return Json{ { "a", a.toJson() }, { "b", b.toJson() } }; }
        void load(const Json& j) override {
            a = Pos2::fromJson(j.value("a", Json::object()));
            b = Pos2::fromJson(j.value("b", Json::object()));
        }

        void tessellate(std::vector<Pos>& out) const override { out.push_back(a); out.push_back(b); }
        float distanceTo(Pos p) const override { return (p - closestOnSegment(p, a, b)).pythag(); }
        void anchors(std::vector<Pos>& out) const override { out.push_back(a); out.push_back(b); }
        void snapPoints(std::vector<Pos>& out) const override {
            out.push_back(a); out.push_back(b); out.push_back((a + b) * 0.5f);   // + midpoint
        }
        void asLineSegments(std::vector<std::pair<Pos, Pos>>& out) const override { out.push_back({ a, b }); }
        bool footOnCurve(Pos p, Pos& out) const override { out = closestOnSegment(p, a, b); return true; }
        void controlPoints(std::vector<Pos2*>& out) override { out.push_back(&a); out.push_back(&b); }

        // Slide the line sideways by `amount` along its left-hand normal (the +90deg
        // rotation of a->b); a negative amount slides the other way.
        void offset(float amount) override {
            Pos d = b - a; float len = d.pythag();
            if (len < 1e-9f) { return; }
            Pos n(-d.y / len, d.x / len);
            a -= n * amount;
            b -= n * amount;
        }
    };

    // Circle -- centre c, radius point a (|a - c| is the radius), and chirality
    // marker b: a point on the circle a quarter-turn (pi/4) from a. The *side* b sits
    // on is the orientation -- b a quarter-turn CCW of a is a CCW circle, CW is a CW
    // circle. Exactly like an arc (c, a, b), orientation lives in the points: no
    // signed radius, no bool. So a CW circle offsets opposite to a CCW one, and an
    // inset that drives the radius through zero turns the circle inside-out -- the
    // radius re-emerges positive on the far side and the chirality flips.
    struct Circle2 : public Stoicheion {

        static constexpr float Right = TAU * 0.25f;   // pi/2 (a quarter turn)

        Pos2 c, a, b;

        Circle2() = default;
        // Centre + radius: a CCW circle (a on +x, b a quarter-turn CCW of it).
        Circle2(const Pos& c, float r)
            : c(c), a(c + Pos(r, 0.0f)), b(c + Pos::fromAngle(Right) * r) {}
        Circle2(const Pos& c, const Pos& a, const Pos& b) : c(c), a(a), b(b) {}

        // The unique circle through three points; its chirality follows their winding
        // (p0 -> p1 -> p2 counterclockwise gives a CCW circle).
        static Circle2 ThroughThreePoints(Pos p0, Pos p1, Pos p2) {
            Pos o = circumcentre(p0, p1, p2);
            Pos ra = p0 - o;
            bool ccw = (p1 - p0).cross(p2 - p0) >= 0.0f;
            Pos marker = ccw ? (o + Pos(-ra.y, ra.x)) : (o + Pos(ra.y, -ra.x));   // b at +/-90 deg
            return Circle2(o, p0, marker);
        }
        // The circle having `a`-`b` as a diameter (CCW).
        static Circle2 Diameter(Pos a, Pos b) {
            return Circle2((a + b) * 0.5f, (a - b).pythag() * 0.5f);
        }

        float radius() const { return (a - c).pythag(); }
        int chirality() const { return ((a - c).cross(b - c) >= 0.0f) ? 1 : -1; }   // +1 CCW, -1 CW

        const char* kind() const override { return "circle"; }
        std::unique_ptr<Stoicheion> clone() const override { return std::make_unique<Circle2>(*this); }
        Json data() const override { return Json{ { "c", c.toJson() }, { "a", a.toJson() }, { "b", b.toJson() } }; }
        void load(const Json& j) override {
            c = Pos2::fromJson(j.value("c", Json::object()));
            a = Pos2::fromJson(j.value("a", Json::object()));
            b = Pos2::fromJson(j.value("b", Json::object()));
        }

        void tessellate(std::vector<Pos>& out) const override {
            sampleArc(out, c, radius(), (a - c).angle(), TAU, spanSteps(TAU));
        }
        float distanceTo(Pos p) const override { return std::fabs((p - c).pythag() - radius()); }
        void anchors(std::vector<Pos>& out) const override { out.push_back(c); out.push_back(a); out.push_back(b); }
        void snapPoints(std::vector<Pos>& out) const override {
            float r = radius();
            out.push_back(c); out.push_back(a); out.push_back(b);       // centre + handles
            out.push_back(c + Pos(r, 0.0f)); out.push_back(c + Pos(-r, 0.0f));   // + quadrants
            out.push_back(c + Pos(0.0f, r)); out.push_back(c + Pos(0.0f, -r));
        }
        bool asCircle(Pos& cc, float& rr, float& a0, float& sweep) const override {
            cc = c; rr = radius(); a0 = (a - c).angle(); sweep = TAU; return true;
        }
        bool footOnCurve(Pos p, Pos& out) const override {
            float r = radius();
            Pos d = p - c; float dd = d.pythag();
            out = (dd > 1e-6f) ? (c + d / dd * r) : (c + Pos(r, 0.0f));
            return true;
        }
        void controlPoints(std::vector<Pos2*>& out) override { out.push_back(&c); out.push_back(&a); out.push_back(&b); }

        // b is the chirality marker, not a solved point, and is kept canonical: a
        // quarter-turn (pi/2) from a, on the side it currently sits. Re-seated after
        // any move/solve so it always reads "at the proper location".
        int chiralitySlot() const override { return 2; }
        void normalize() override {
            Pos ra = a - c;
            b = (chirality() > 0) ? (c + Pos(-ra.y, ra.x))    // +90 deg (CCW)
                                  : (c + Pos(ra.y, -ra.x));   // -90 deg (CW)
        }

        // Concentric offset, chirality-aware. A CCW circle shrinks for a negative
        // amount; a CW circle grows -- opposite, just as a CW chain offsets opposite
        // to a CCW one. Driving the radius through zero turns the circle inside-out:
        // it re-emerges on the far side with the chirality flipped (b swaps sides).
        void offset(float amount) override {
            float r = radius();
            if (r < 1e-9f) { return; }
            int s = chirality();
            float nr = r + amount * static_cast<float>(s);
            float ang = (a - c).angle();
            if (nr < 0.0f) { nr = -nr; ang += TAU * 0.5f; s = -s; }     // through the singularity
            a = c + Pos::fromAngle(ang) * nr;
            b = c + Pos::fromAngle(ang + static_cast<float>(s) * Right) * nr;
        }
    };

    // Arc -- centre c, endpoints a and b, and a *through-point* d that fixes which
    // way round we travel: the arc runs "from A to B through D". Whether D sits on the
    // CCW or CW side of the chord (about the centre) is the chirality -- so the same
    // pair of endpoints can be the CCW arc or the CW arc, and it offsets accordingly,
    // just like a circle's marker point. Only D's *angle* about the centre matters;
    // its distance is free.
    struct Arc2 : public Stoicheion {

        Pos2 c, a, b, d;

        Arc2() = default;
        // Two-endpoint form: a CCW arc, D placed at the CCW midpoint.
        Arc2(const Pos& c, const Pos& a, const Pos& b) : c(c), a(a), b(b) {
            float ang = (a - c).angle() + wrapTau((b - c).angle() - (a - c).angle()) * 0.5f;
            d = c + Pos::fromAngle(ang) * (a - c).pythag();
        }
        Arc2(const Pos& c, const Pos& a, const Pos& b, const Pos& d) : c(c), a(a), b(b), d(d) {}

        // Construct the arc through `a` and `b` that is tangent to `tanA` at a and
        // `tanB` at b. The centre is where the perpendiculars to the two tangents
        // meet (well-defined unless the tangents are parallel) -- and for a fillet at
        // an offset corner that meeting point is exactly the original (pre-inset)
        // corner, with a and b equidistant from it. The travel direction (D / the
        // sweep) is chosen so the arc leaves a heading along tanA.
        static Arc2 Tangent(Pos a, Pos b, Pos tanA, Pos tanB) {
            Pos na(-tanA.y, tanA.x), nb(-tanB.y, tanB.x);   // perpendiculars to the tangents
            float rxs = na.cross(nb);
            Pos o = (std::fabs(rxs) > 1e-9f) ? (a + na * ((b - a).cross(nb) / rxs))
                                             : ((a + b) * 0.5f);   // parallel: degenerate fallback
            float radius = (a - o).pythag();
            Pos radial = a - o;
            Pos ccwTan(-radial.y, radial.x);                       // CCW tangent at a
            int s = (tanA.dot(ccwTan) >= 0.0f) ? 1 : -1;           // does tanA head CCW?
            float angA = radial.angle();
            float span = (s > 0) ? wrapTau((b - o).angle() - angA) : wrapTau(angA - (b - o).angle());
            float midAng = angA + static_cast<float>(s) * span * 0.5f;
            return Arc2(o, a, b, o + Pos::fromAngle(midAng) * radius);
        }

        // The arc that passes through three points, from p0 to p2 by way of p1 -- p1
        // is the through-point, so it fixes both which arc and its chirality.
        static Arc2 ThreePoint(Pos p0, Pos p1, Pos p2) {
            return Arc2(circumcentre(p0, p1, p2), p0, p2, p1);
        }

        float radius() const { return (a - c).pythag(); }

        // +1 if we travel CCW from A to B (D is on the CCW side), -1 if CW.
        int chirality() const {
            float angA = (a - c).angle();
            float ccwB = wrapTau((b - c).angle() - angA);
            float ccwD = wrapTau((d - c).angle() - angA);
            return (ccwD <= ccwB) ? 1 : -1;
        }

        // The covered arc as a CCW start angle + positive sweep (the half through D).
        // Drawing / hit-testing / intersection care about the point set, not the
        // travel direction, so they use this.
        void range(float& a0, float& sweep) const {
            float angA = (a - c).angle();
            float ccwB = wrapTau((b - c).angle() - angA);
            float ccwD = wrapTau((d - c).angle() - angA);
            if (ccwD <= ccwB) { a0 = angA; sweep = ccwB; }                    // CCW: A -> B through D
            else              { a0 = (b - c).angle(); sweep = TAU - ccwB; }   // CW: the complementary half
        }

        const char* kind() const override { return "arc"; }
        std::unique_ptr<Stoicheion> clone() const override { return std::make_unique<Arc2>(*this); }
        Json data() const override {
            return Json{ { "c", c.toJson() }, { "a", a.toJson() }, { "b", b.toJson() }, { "d", d.toJson() } };
        }
        void load(const Json& j) override {
            c = Pos2::fromJson(j.value("c", Json::object()));
            a = Pos2::fromJson(j.value("a", Json::object()));
            b = Pos2::fromJson(j.value("b", Json::object()));
            d = Pos2::fromJson(j.value("d", Json::object()));
        }

        void tessellate(std::vector<Pos>& out) const override {
            float r = radius(); if (r <= 0.0f) { return; }
            float a0, sweep; range(a0, sweep);
            sampleArc(out, c, r, a0, sweep, spanSteps(sweep));
        }
        float distanceTo(Pos p) const override {
            float r = radius();
            if (r <= 0.0f) { return (p - a).pythag(); }
            float a0, sweep; range(a0, sweep);
            float aP = wrapTau((p - c).angle() - a0);
            if (aP <= sweep) { return std::fabs((p - c).pythag() - r); }
            return std::min((p - a).pythag(), (p - b).pythag());
        }
        // Anchors mirror controlPoints (the solver and drag rely on the order):
        // centre, the two endpoints, and the through-point d.
        void anchors(std::vector<Pos>& out) const override {
            out.push_back(c); out.push_back(a); out.push_back(b); out.push_back(d);
        }
        void snapPoints(std::vector<Pos>& out) const override {
            out.push_back(c); out.push_back(a); out.push_back(b);
            float r = radius();
            if (r > 0.0f) { float a0, sweep; range(a0, sweep); out.push_back(c + Pos::fromAngle(a0 + sweep * 0.5f) * r); }
        }
        bool asCircle(Pos& cc, float& rr, float& a0, float& sweep) const override {
            float r = radius();
            if (r <= 0.0f) { return false; }
            cc = c; rr = r; range(a0, sweep); return true;
        }
        bool footOnCurve(Pos p, Pos& out) const override {
            float r = radius(); if (r <= 0.0f) { return false; }
            float a0, sweep; range(a0, sweep);
            float aP = wrapTau((p - c).angle() - a0);
            if (aP > sweep) { return false; }
            Pos dir = p - c; float dd = dir.pythag();
            out = (dd < 1e-6f) ? Pos(a) : (c + dir / dd * r);
            return true;
        }
        void controlPoints(std::vector<Pos2*>& out) override {
            out.push_back(&c); out.push_back(&a); out.push_back(&b); out.push_back(&d);
        }

        // Endpoints a and b must stay equidistant from centre c (both on the circle).
        // Defined out of line, once MotionField is complete.
        void relaxInherent(MotionField& field, Id id, float rate) const override;

        // Concentric offset, chirality-aware. The chirality is the travel direction
        // A -> B through D: CCW (+1) behaves like a CCW boundary, so a negative amount
        // shrinks it; CW (-1) reverses the sense, so the same amount grows it. Driving
        // the radius through zero flips the points to the antipode ("flips around").
        void offset(float amount) override {
            float rad = radius();
            if (rad < 1e-9f) { return; }
            float nr = rad + amount * static_cast<float>(chirality());
            a = c + (a - c) / rad * nr;
            b = c + (b - c) / rad * nr;
            float ld = (d - c).pythag();
            if (ld > 1e-9f) { d = c + (d - c) / ld * nr; }   // keep D's angle, on the new circle
        }

        // d is the chirality marker, not a solved point, and is kept canonical:
        // exactly the midpoint of the half it currently indicates, on the circle.
        // Re-seated after any move/solve so it stays "at the proper location".
        int chiralitySlot() const override { return 3; }
        void normalize() override {
            float r = radius();
            if (r < 1e-9f) { return; }
            float a0, sweep; range(a0, sweep);
            d = c + Pos::fromAngle(a0 + sweep * 0.5f) * r;
        }
    };

    // Ellipse (centre + conjugate semi-axes U, V; only the centre is an absolute,
    // movable point -- translating the entity must not disturb the axis vectors)
    //--------------------------------------------------
    struct Ellipse2 : public Stoicheion {

        Pos2 c, u, v;

        Ellipse2() = default;
        Ellipse2(const Pos& c, const Pos& u, const Pos& v) : c(c), u(u), v(v) {}

        const char* kind() const override { return "ellipse"; }
        std::unique_ptr<Stoicheion> clone() const override { return std::make_unique<Ellipse2>(*this); }
        Json data() const override { return Json{ { "c", c.toJson() }, { "u", u.toJson() }, { "v", v.toJson() } }; }
        void load(const Json& j) override {
            c = Pos2::fromJson(j.value("c", Json::object()));
            u = Pos2::fromJson(j.value("u", Json::object()));
            v = Pos2::fromJson(j.value("v", Json::object()));
        }

        void tessellate(std::vector<Pos>& out) const override { sampleEllipse(out, c, u, v, 0.0f, TAU, spanSteps(TAU)); }
        float distanceTo(Pos p) const override {
            Pos foot; footNearest(p, 0.0f, TAU, foot); return (p - foot).pythag();
        }
        void anchors(std::vector<Pos>& out) const override { out.push_back(c); }
        void snapPoints(std::vector<Pos>& out) const override {
            out.push_back(c);                            // centre + the four axis ends
            out.push_back(c + u); out.push_back(c - u);
            out.push_back(c + v); out.push_back(c - v);
        }
        bool footOnCurve(Pos p, Pos& out) const override { footNearest(p, 0.0f, TAU, out); return true; }
        void controlPoints(std::vector<Pos2*>& out) override { out.push_back(&c); }

        // Rough in-place offset (kept for previews/quick edits): grow each conjugate
        // semi-axis, scaled by handedness sign(u x v). This only matches the true
        // offset at the four vertices -- offsetInto() is the analytically correct one.
        void offset(float amount) override {
            float d = amount * (u.cross(v) >= 0.0f ? 1.0f : -1.0f);
            float lu = u.pythag(), lv = v.pythag();
            if (lu > 1e-9f) { u += u / lu * d; }
            if (lv > 1e-9f) { v += v / lv * d; }
        }

        // The analytically-equidistant offset: every point pushed along its real
        // normal by `amount` (handedness-scaled: CCW insets, CW grows), emitted as a
        // closed polyline -- the exact parallel curve, which is not an ellipse.
        void offsetInto(float amount, std::vector<std::unique_ptr<Stoicheion>>& out) const override {
            float d = amount * (u.cross(v) >= 0.0f ? 1.0f : -1.0f);
            int steps = spanSteps(TAU);
            Pos prev = ellipseOffsetPoint(c, u, v, 0.0f, d);
            for (int i = 1; i <= steps; i++) {
                float t = TAU * (static_cast<float>(i) / static_cast<float>(steps));
                Pos cur = ellipseOffsetPoint(c, u, v, t, d);
                out.push_back(std::make_unique<Segment2>(prev, cur));
                prev = cur;
            }
        }

      protected:
        // Coarse nearest-point search over a parameter span (no closed form).
        void footNearest(Pos p, float t0, float span, Pos& out) const {
            const int N = 96; float best = 1e30f;
            for (int i = 0; i <= N; i++) {
                Pos e = ellipsePointAt(c, u, v, t0 + span * (static_cast<float>(i) / N));
                float d = (p - e).pythag();
                if (d < best) { best = d; out = e; }
            }
        }
    };

    // Elliptical arc (swept by increasing parameter a0 -> a1, i.e. CCW)
    //--------------------------------------------------
    struct EllipseArc2 : public Stoicheion {

        Pos2 c, u, v;
        float a0 = 0.0f, a1 = 0.0f;

        EllipseArc2() = default;
        EllipseArc2(const Pos& c, const Pos& u, const Pos& v, float a0, float a1)
            : c(c), u(u), v(v), a0(a0), a1(a1) {}

        // Construct the elliptical quarter-arc through a and b, tangent to tanA at a
        // and tanB at b. The centre O is placed so that (a-O) is parallel to tanB and
        // (b-O) to tanA -- i.e. a and b are the ends of *conjugate* semi-diameters, so
        // U = a-O and V = b-O are conjugate axes and the arc from parameter 0 (a) to
        // pi/2 (b) is tangent to both edges. Unlike a circular arc this is unique only
        // up to the conjugate-diameter choice; this is the natural canonical one.
        static EllipseArc2 Tangent(Pos a, Pos b, Pos tanA, Pos tanB) {
            float rxs = tanB.cross(tanA);
            Pos o = (std::fabs(rxs) > 1e-9f) ? (a + tanB * ((b - a).cross(tanA) / rxs))
                                             : ((a + b) * 0.5f);   // parallel tangents: fallback
            return EllipseArc2(o, a - o, b - o, 0.0f, TAU * 0.25f);
        }

        float span() const { return wrapTau(a1 - a0); }
        Pos startPoint() const { return ellipsePointAt(c, u, v, a0); }
        Pos endPoint() const { return ellipsePointAt(c, u, v, a0 + span()); }

        const char* kind() const override { return "ellipseArc"; }
        std::unique_ptr<Stoicheion> clone() const override { return std::make_unique<EllipseArc2>(*this); }
        Json data() const override {
            return Json{ { "c", c.toJson() }, { "u", u.toJson() }, { "v", v.toJson() }, { "a0", a0 }, { "a1", a1 } };
        }
        void load(const Json& j) override {
            c = Pos2::fromJson(j.value("c", Json::object()));
            u = Pos2::fromJson(j.value("u", Json::object()));
            v = Pos2::fromJson(j.value("v", Json::object()));
            a0 = j.value("a0", 0.0f); a1 = j.value("a1", 0.0f);
        }

        void tessellate(std::vector<Pos>& out) const override {
            float sp = span(); sampleEllipse(out, c, u, v, a0, sp, spanSteps(sp));
        }
        float distanceTo(Pos p) const override {
            Pos foot; footNearest(p, foot); return (p - foot).pythag();
        }
        void anchors(std::vector<Pos>& out) const override {
            out.push_back(c); out.push_back(startPoint()); out.push_back(endPoint());
        }
        bool footOnCurve(Pos p, Pos& out) const override { footNearest(p, out); return true; }
        void controlPoints(std::vector<Pos2*>& out) override { out.push_back(&c); }

        // Rough in-place offset (kept for previews): grow/shrink the semi-axes,
        // handedness-scaled. offsetInto() is the analytically correct one.
        void offset(float amount) override {
            float d = amount * (u.cross(v) >= 0.0f ? 1.0f : -1.0f);
            float lu = u.pythag(), lv = v.pythag();
            if (lu > 1e-9f) { u += u / lu * d; }
            if (lv > 1e-9f) { v += v / lv * d; }
        }

        // The analytically-equidistant offset over the arc's parameter span, emitted
        // as a polyline -- the exact parallel curve (handedness-scaled: CCW insets).
        void offsetInto(float amount, std::vector<std::unique_ptr<Stoicheion>>& out) const override {
            float d = amount * (u.cross(v) >= 0.0f ? 1.0f : -1.0f);
            float sp = span();
            int steps = spanSteps(sp);
            Pos prev = ellipseOffsetPoint(c, u, v, a0, d);
            for (int i = 1; i <= steps; i++) {
                float t = a0 + sp * (static_cast<float>(i) / static_cast<float>(steps));
                Pos cur = ellipseOffsetPoint(c, u, v, t, d);
                out.push_back(std::make_unique<Segment2>(prev, cur));
                prev = cur;
            }
        }

      protected:
        void footNearest(Pos p, Pos& out) const {
            const int N = 96; float sp = span(), best = 1e30f;
            for (int i = 0; i <= N; i++) {
                Pos e = ellipsePointAt(c, u, v, a0 + sp * (static_cast<float>(i) / N));
                float d = (p - e).pythag();
                if (d < best) { best = d; out = e; }
            }
        }
    };

    // Factory: reconstruct an entity from its JSON envelope.
    //--------------------------------------------------
    inline std::unique_ptr<Stoicheion> stoicheionFromJson(const Json& j) {

        std::string kind = j.value("kind", std::string());
        std::unique_ptr<Stoicheion> e;

        if      (kind == "point")      { e = std::make_unique<Point2>(); }
        else if (kind == "segment")    { e = std::make_unique<Segment2>(); }
        else if (kind == "circle")     { e = std::make_unique<Circle2>(); }
        else if (kind == "arc")        { e = std::make_unique<Arc2>(); }
        else if (kind == "ellipse")    { e = std::make_unique<Ellipse2>(); }
        else if (kind == "ellipseArc") { e = std::make_unique<EllipseArc2>(); }
        else { return nullptr; }

        e->load(j);
        e->id = idFromHex(j.value("id", std::string()));
        if (e->id == 0) { e->id = newId(); }
        e->construction = j.value("construction", false);
        e->locked = j.value("locked", false);
        return e;
    }

    // ===============================================================
    // Relation -- the single concept that ties entities together. There is no
    // separate "constraint": a dimension is a Distance relation, horizontal/vertical
    // is a Parallel relation, a lock is a Lock relation. All of them are relations.
    //
    // Mirrors the Stoicheion pattern: an overridable base whose concrete types each
    // store their own data and serialise themselves under a "kind" tag. A relation
    // references the entities it relates by their stable ids (so it survives edits
    // and re-ordering). Concrete relation types (coincident, parallel, distance,
    // tangent, ...) arrive on top of this scaffold.
    // ===============================================================
    // A reference to one defining point of an entity: the entity's stable id plus
    // the slot index into its control points (segment end 0/1, circle centre 0...).
    // This is how a relation names a point without knowing what kind of entity owns
    // it. Resolved to a mutable Pos2* through the entity at apply time.
    struct PointRef {
        Id entity = 0;
        int slot = 0;

        bool operator==(const PointRef& o) const { return entity == o.entity && slot == o.slot; }

        Json toJson() const { return Json{ { "e", idToHex(entity) }, { "s", slot } }; }
        static PointRef fromJson(const Json& j) {
            return { idFromHex(j.value("e", std::string())), j.value("s", 0) };
        }
    };

    // The instantaneous freedom of a point: which displacements it can accept. The
    // master stroke -- capacity is not binary. A point may be Free (the whole
    // plane), constrained to a Line (one direction only), or Locked (no motion).
    // A proposed displacement is *masked* onto the allowed subspace before it is
    // applied; capacities compose by intersection back along the authority chain.
    struct Capacity {
        enum Kind { Free, Line, Locked };
        Kind kind = Free;
        Pos dir;                       // unit direction when Line

        static Capacity free()        { return { Free,   Pos() }; }
        static Capacity locked()      { return { Locked, Pos() }; }
        static Capacity line(Pos d)   { float l = d.pythag(); return { Line, (l > 1e-9f) ? d / l : Pos() }; }

        // Project a proposed displacement onto the allowed subspace (the signed dot
        // product picks how far along an allowed line the proposal carries).
        Pos mask(Pos delta) const {
            if (kind == Locked) { return Pos(0.0f, 0.0f); }
            if (kind == Line)   { return dir * delta.dot(dir); }
            return delta;
        }

        // Intersect two capacities -- the point must satisfy both.
        static Capacity combine(Capacity a, Capacity b) {
            if (a.kind == Locked || b.kind == Locked) { return locked(); }
            if (a.kind == Free) { return b; }
            if (b.kind == Free) { return a; }
            // Both lines: parallel ⇒ the same line; otherwise only the origin ⇒ locked.
            return (std::fabs(a.dir.cross(b.dir)) < 1e-6f) ? a : locked();
        }
    };

    // The working buffer for the interactive relaxation ("annealing") solver. Every
    // control point becomes a node holding a scratch position and whether it is free
    // to move. Relations read neighbours' positions and nudge the movable ones toward
    // satisfaction; pinned nodes (grounded datums, or the points the user is holding)
    // are the boundary the motion distributes between. This is a soft, position-based
    // relaxation -- it spreads motion realistically, it does not assert hard rules.
    // A point the user is holding during a drag, and where they are holding it.
    struct DragPoint { PointRef ref; Pos target; };

    struct MotionField {
        struct Node { PointRef ref; Pos pos; bool movable = true; };
        std::vector<Node> nodes;

        int find(const PointRef& p) const {
            for (size_t i = 0; i < nodes.size(); i++) { if (nodes[i].ref == p) { return static_cast<int>(i); } }
            return -1;
        }
        Pos pos(const PointRef& p) const { int i = find(p); return (i < 0) ? Pos() : nodes[i].pos; }
        // Freedom weight: 0 if pinned/absent, else 1. (Friction weighting can refine this.)
        float freedom(const PointRef& p) const { int i = find(p); return (i < 0 || !nodes[i].movable) ? 0.0f : 1.0f; }
        // Move a point by a delta -- ignored if the point is pinned.
        void nudge(const PointRef& p, Pos d) { int i = find(p); if (i >= 0 && nodes[i].movable) { nodes[i].pos = nodes[i].pos + d; } }
        // Place a point exactly -- ignored if the point is pinned (used by the weld).
        void set(const PointRef& p, Pos pos) { int i = find(p); if (i >= 0 && nodes[i].movable) { nodes[i].pos = pos; } }
    };

    // Arc invariant: the two endpoints share the centre's radius. Distributed as a
    // position-based constraint across whichever of {c, a, b} are free -- so dragging
    // an endpoint to a coincidence slides the *centre/start* to keep the arc circular
    // and the endpoint genuinely on the curve (never a free-floating control point).
    inline void Arc2::relaxInherent(MotionField& field, Id id, float rate) const {
        PointRef rc{ id, 0 }, ra{ id, 1 }, rb{ id, 2 };
        Pos C = field.pos(rc), A = field.pos(ra), B = field.pos(rb);
        float fc = field.freedom(rc), fa = field.freedom(ra), fb = field.freedom(rb);
        float la = (A - C).pythag(), lb = (B - C).pythag();
        if (la < 1e-6f || lb < 1e-6f) { return; }
        Pos ua = (A - C) / la, ub = (B - C) / lb;       // unit radials
        Pos gc = ub - ua;                               // gradient of (la-lb) w.r.t. C
        float denom = fa + fb + fc * gc.dot(gc);        // inverse-mass weighted
        if (denom < 1e-9f) { return; }
        float lambda = (la - lb) / denom;
        field.nudge(ra, ua * (-lambda * fa * rate));
        field.nudge(rb, ub * ( lambda * fb * rate));
        field.nudge(rc, gc * (-lambda * fc * rate));
    }

    struct Relation {

        Id id = 0;                   // stable unique identity (assigned on commit)

        virtual ~Relation() = default;

        virtual const char* kind() const = 0;
        virtual std::unique_ptr<Relation> clone() const = 0;
        virtual Json data() const = 0;          // type-specific payload
        virtual void load(const Json& j) = 0;   // read type-specific payload

        // Enforce this relation on the geometry, type-agnostically: the relation
        // resolves the entities it references by id (via `lookup`) and commands
        // their points. It never learns what kind of entity it operates on.
        virtual void apply(const std::function<Stoicheion*(Id)>& lookup) {}

        // Solve-order topology: the point this relation *determines* (its follower)
        // and the points that determine it (its authorities/masters). Used to rank
        // relations so they resolve roots-first.
        virtual PointRef follower() const { return {}; }
        virtual void masters(std::vector<PointRef>& out) const {}

        // If this relation rigidly ties two points to the same position (a
        // coincidence), report them -- so a drag can move the whole cluster.
        virtual bool coincidentPair(PointRef& a, PointRef& b) const { return false; }

        // How this relation restricts the motion capacity of point `p`. `resolveCap`
        // computes the full capacity of any other point (so a relation can defer to
        // the authority chain), and `posOf` reads a point's current position (a
        // distance needs it to find the tangent). Default: no restriction.
        virtual Capacity capacity(const PointRef& p,
                                  const std::function<Capacity(const PointRef&)>& resolveCap,
                                  const std::function<Pos(const PointRef&)>& posOf) const {
            return Capacity::free();
        }

        // Soft motion distribution for the interactive solver. Nudge this relation's
        // movable points a fraction `rate` toward satisfaction, splitting each
        // correction between its points in proportion to their freedom (the freer
        // point yields more) so motion spreads rather than snapping. Pinned points
        // act as anchors. Default: a relation distributes no motion.
        virtual void relax(MotionField& field, float rate) const {}

        // How badly this relation is currently violated, in world units (0 = exactly
        // satisfied). Used to detect whether a freshly proposed relation can coexist
        // with the existing ones or genuinely conflicts. Default: never violated.
        virtual float residual(const std::function<Pos(const PointRef&)>& posOf) const { return 0.0f; }

        Json toJson() const {
            Json j = data();
            j["id"] = idToHex(id);
            j["kind"] = kind();
            return j;
        }
    };

    // Set point `r`'s position on its entity (resolved via `lookup`); no-op if the
    // entity or slot is gone. Reading is the const-correct mirror.
    inline bool readPoint(const std::function<Stoicheion*(Id)>& lookup, const PointRef& r, Pos& out) {
        Stoicheion* e = lookup(r.entity);
        if (!e) { return false; }
        std::vector<Pos2*> cps;
        e->controlPoints(cps);
        if (r.slot < 0 || r.slot >= static_cast<int>(cps.size())) { return false; }
        out = *cps[static_cast<size_t>(r.slot)];
        return true;
    }
    inline bool writePoint(const std::function<Stoicheion*(Id)>& lookup, const PointRef& r, Pos p) {
        Stoicheion* e = lookup(r.entity);
        if (!e || e->locked) { return false; }   // datums never move
        std::vector<Pos2*> cps;
        e->controlPoints(cps);
        if (r.slot < 0 || r.slot >= static_cast<int>(cps.size())) { return false; }
        *cps[static_cast<size_t>(r.slot)] = p;
        return true;
    }

    // Coincident: point B is held equal to point A. A is the authority (the master);
    // B follows. (Symmetric in meaning, directional in resolution -- the order is
    // the authority order.)
    struct Coincident : public Relation {

        PointRef a, b;

        Coincident() = default;
        Coincident(const PointRef& a, const PointRef& b) : a(a), b(b) {}

        const char* kind() const override { return "coincident"; }
        std::unique_ptr<Relation> clone() const override { return std::make_unique<Coincident>(*this); }
        Json data() const override { return Json{ { "a", a.toJson() }, { "b", b.toJson() } }; }
        void load(const Json& j) override {
            a = PointRef::fromJson(j.value("a", Json::object()));
            b = PointRef::fromJson(j.value("b", Json::object()));
        }

        void apply(const std::function<Stoicheion*(Id)>& lookup) override {
            Pos pa;
            if (readPoint(lookup, a, pa)) { writePoint(lookup, b, pa); }   // B := A
        }

        PointRef follower() const override { return b; }
        void masters(std::vector<PointRef>& out) const override { out.push_back(a); }
        bool coincidentPair(PointRef& pa, PointRef& pb) const override { pa = a; pb = b; return true; }

        // The slave inherits its master's freedom: it can only go where A can go.
        Capacity capacity(const PointRef& p,
                          const std::function<Capacity(const PointRef&)>& resolveCap,
                          const std::function<Pos(const PointRef&)>& posOf) const override {
            return (p == b) ? resolveCap(a) : Capacity::free();
        }

        // Pull A and B together, sharing the closing motion by their freedom.
        void relax(MotionField& field, float rate) const override {
            float fa = field.freedom(a), fb = field.freedom(b);
            if (fa + fb <= 0.0f) { return; }
            Pos err = field.pos(b) - field.pos(a);          // gap to close
            field.nudge(a, err * ( (fa / (fa + fb)) * rate));
            field.nudge(b, err * (-(fb / (fa + fb)) * rate));
        }

        float residual(const std::function<Pos(const PointRef&)>& posOf) const override {
            return (posOf(b) - posOf(a)).pythag();
        }
    };

    // Lock: a point is pinned in place (it is held at `at`). The fundamental zero-
    // freedom constraint -- even the origin is locked to mathematical (0,0) by one
    // of these, rather than being locked by fiat.
    struct Lock : public Relation {

        PointRef point;
        Pos2 at;

        Lock() = default;
        Lock(const PointRef& point, Pos at) : point(point), at(at) {}

        const char* kind() const override { return "lock"; }
        std::unique_ptr<Relation> clone() const override { return std::make_unique<Lock>(*this); }
        Json data() const override { return Json{ { "point", point.toJson() }, { "at", at.toJson() } }; }
        void load(const Json& j) override {
            point = PointRef::fromJson(j.value("point", Json::object()));
            at = Pos2::fromJson(j.value("at", Json::object()));
        }

        void apply(const std::function<Stoicheion*(Id)>& lookup) override {
            writePoint(lookup, point, at);   // re-assert the locked position
        }

        PointRef follower() const override { return point; }   // grounded root: no masters

        Capacity capacity(const PointRef& p,
                          const std::function<Capacity(const PointRef&)>& resolveCap,
                          const std::function<Pos(const PointRef&)>& posOf) const override {
            return (p == point) ? Capacity::locked() : Capacity::free();
        }

        float residual(const std::function<Pos(const PointRef&)>& posOf) const override {
            return (posOf(point) - at).pythag();
        }
    };

    // Distance: the separation |A - B| is held at `d`. B is the authority-follower
    // (B adjusts to lie at distance d from A along the current direction). This is
    // the relation behind every "length" and "radius" -- a dimension is a Distance.
    struct Distance : public Relation {

        PointRef a, b;
        float d = 0.0f;

        Distance() = default;
        Distance(const PointRef& a, const PointRef& b, float d) : a(a), b(b), d(d) {}

        const char* kind() const override { return "distance"; }
        std::unique_ptr<Relation> clone() const override { return std::make_unique<Distance>(*this); }
        Json data() const override { return Json{ { "a", a.toJson() }, { "b", b.toJson() }, { "d", d } }; }
        void load(const Json& j) override {
            a = PointRef::fromJson(j.value("a", Json::object()));
            b = PointRef::fromJson(j.value("b", Json::object()));
            d = j.value("d", 0.0f);
        }

        void apply(const std::function<Stoicheion*(Id)>& lookup) override {
            Pos pa, pb;
            if (!readPoint(lookup, a, pa) || !readPoint(lookup, b, pb)) { return; }
            Pos dir = pb - pa;
            float len = dir.pythag();
            Pos nb = (len > 1e-6f) ? (pa + dir / len * d) : (pa + Pos(d, 0.0f));
            writePoint(lookup, b, nb);   // B re-pinned to distance d from A
        }

        PointRef follower() const override { return b; }
        void masters(std::vector<PointRef>& out) const override { out.push_back(a); }

        // If the partner is fixed, the point is confined to a circle of radius d --
        // its instantaneous freedom is the tangent (perpendicular to the radius). If
        // the partner is free, the distance imposes no *absolute* restriction.
        Capacity capacity(const PointRef& p,
                          const std::function<Capacity(const PointRef&)>& resolveCap,
                          const std::function<Pos(const PointRef&)>& posOf) const override {
            PointRef partner;
            if      (p == a) { partner = b; }
            else if (p == b) { partner = a; }
            else             { return Capacity::free(); }

            if (resolveCap(partner).kind != Capacity::Locked) { return Capacity::free(); }
            Pos radius = posOf(p) - posOf(partner);
            return Capacity::line(Pos(-radius.y, radius.x));   // tangent to the circle
        }

        // Restore the length |A-B| = d. The correction runs along the line A->B, so
        // pulling one point drags the other along that direction -- motion transfers
        // by the dot product with the connecting direction, shared by freedom.
        void relax(MotionField& field, float rate) const override {
            float fa = field.freedom(a), fb = field.freedom(b);
            if (fa + fb <= 0.0f) { return; }
            Pos delta = field.pos(b) - field.pos(a);
            float len = delta.pythag();
            if (len < 1e-6f) { return; }
            Pos corr = delta / len * (len - d);             // >0 ⇒ too long: pull together
            field.nudge(a, corr * ( (fa / (fa + fb)) * rate));
            field.nudge(b, corr * (-(fb / (fa + fb)) * rate));
        }

        float residual(const std::function<Pos(const PointRef&)>& posOf) const override {
            return std::fabs((posOf(b) - posOf(a)).pythag() - d);
        }
    };

    // Parallel: the segment A->B runs parallel to a *reference* segment refA->refB.
    // The direction is not stored -- it is read live from the reference's points, so
    // the reference genuinely represents the direction in the graph. "Horizontal"
    // and "vertical" are simply this relation taken against the X and Y axes, whose
    // endpoints are locked datum points. No angle, no special case. B is projected
    // onto the line through A along the reference direction.
    struct Parallel : public Relation {

        PointRef a, b;          // the constrained segment (A anchors, B follows)
        PointRef refA, refB;    // the reference direction (e.g. an axis' two endpoints)

        Parallel() = default;
        Parallel(const PointRef& a, const PointRef& b, const PointRef& refA, const PointRef& refB)
            : a(a), b(b), refA(refA), refB(refB) {}

        const char* kind() const override { return "parallel"; }
        std::unique_ptr<Relation> clone() const override { return std::make_unique<Parallel>(*this); }
        Json data() const override {
            return Json{ { "a", a.toJson() }, { "b", b.toJson() },
                         { "refA", refA.toJson() }, { "refB", refB.toJson() } };
        }
        void load(const Json& j) override {
            a    = PointRef::fromJson(j.value("a", Json::object()));
            b    = PointRef::fromJson(j.value("b", Json::object()));
            refA = PointRef::fromJson(j.value("refA", Json::object()));
            refB = PointRef::fromJson(j.value("refB", Json::object()));
        }

        void apply(const std::function<Stoicheion*(Id)>& lookup) override {
            Pos pa, pb, ra, rb;
            if (!readPoint(lookup, a, pa) || !readPoint(lookup, b, pb)) { return; }
            if (!readPoint(lookup, refA, ra) || !readPoint(lookup, refB, rb)) { return; }
            Pos d = (rb - ra).normalized();
            if (d.pythag() < 1e-9f) { return; }
            Pos nb = pa + d * (pb - pa).dot(d);   // B := projection of B onto the line A + t*dir
            writePoint(lookup, b, nb);
        }

        PointRef follower() const override { return b; }
        void masters(std::vector<PointRef>& out) const override {
            out.push_back(a); out.push_back(refA); out.push_back(refB);
        }

        // With the partner fixed and the reference direction pinned, the point may
        // only slide along that direction (the segment must stay parallel).
        Capacity capacity(const PointRef& p,
                          const std::function<Capacity(const PointRef&)>& resolveCap,
                          const std::function<Pos(const PointRef&)>& posOf) const override {
            PointRef partner;
            if      (p == a) { partner = b; }
            else if (p == b) { partner = a; }
            else             { return Capacity::free(); }
            if (resolveCap(partner).kind != Capacity::Locked) { return Capacity::free(); }
            if (resolveCap(refA).kind != Capacity::Locked ||
                resolveCap(refB).kind != Capacity::Locked) { return Capacity::free(); }   // direction not pinned
            return Capacity::line((posOf(refB) - posOf(refA)).normalized());
        }

        // Drive the segment back to parallel by removing the component of (B-A) along
        // the reference *normal* -- the deviation is the dot product of the segment
        // with that normal, distributed between A and B by their freedom.
        void relax(MotionField& field, float rate) const override {
            Pos d = (field.pos(refB) - field.pos(refA)).normalized();
            if (d.pythag() < 1e-9f) { return; }
            Pos n(-d.y, d.x);                               // reference normal
            float fa = field.freedom(a), fb = field.freedom(b);
            if (fa + fb <= 0.0f) { return; }
            float dev = (field.pos(b) - field.pos(a)).dot(n);   // perpendicular error
            field.nudge(a, n * ( dev * (fa / (fa + fb)) * rate));
            field.nudge(b, n * (-dev * (fb / (fa + fb)) * rate));
        }

        float residual(const std::function<Pos(const PointRef&)>& posOf) const override {
            Pos dir = (posOf(refB) - posOf(refA)).normalized();
            if (dir.pythag() < 1e-9f) { return 0.0f; }
            Pos n(-dir.y, dir.x);
            return std::fabs((posOf(b) - posOf(a)).dot(n));
        }
    };

    // Equal: the segment A->B is held the same length as a reference segment
    // refA->refB. Like Distance, but the length is read live from another segment
    // (so "equal" tracks the reference). B follows along its current direction.
    struct Equal : public Relation {

        PointRef a, b;          // the constrained segment (A anchors, B follows)
        PointRef refA, refB;    // the reference segment whose length is matched

        Equal() = default;
        Equal(const PointRef& a, const PointRef& b, const PointRef& refA, const PointRef& refB)
            : a(a), b(b), refA(refA), refB(refB) {}

        const char* kind() const override { return "equal"; }
        std::unique_ptr<Relation> clone() const override { return std::make_unique<Equal>(*this); }
        Json data() const override {
            return Json{ { "a", a.toJson() }, { "b", b.toJson() },
                         { "refA", refA.toJson() }, { "refB", refB.toJson() } };
        }
        void load(const Json& j) override {
            a    = PointRef::fromJson(j.value("a", Json::object()));
            b    = PointRef::fromJson(j.value("b", Json::object()));
            refA = PointRef::fromJson(j.value("refA", Json::object()));
            refB = PointRef::fromJson(j.value("refB", Json::object()));
        }

        void apply(const std::function<Stoicheion*(Id)>& lookup) override {
            Pos pa, pb, ra, rb;
            if (!readPoint(lookup, a, pa) || !readPoint(lookup, b, pb)) { return; }
            if (!readPoint(lookup, refA, ra) || !readPoint(lookup, refB, rb)) { return; }
            float L = (rb - ra).pythag();
            Pos dir = (pb - pa);
            float len = dir.pythag();
            Pos nb = (len > 1e-6f) ? (pa + dir / len * L) : (pa + Pos(L, 0.0f));
            writePoint(lookup, b, nb);
        }

        PointRef follower() const override { return b; }
        void masters(std::vector<PointRef>& out) const override {
            out.push_back(a); out.push_back(refA); out.push_back(refB);
        }

        // With its own anchor fixed and the reference length pinned, the point is
        // confined to a circle of that radius -- its freedom is the tangent.
        Capacity capacity(const PointRef& p,
                          const std::function<Capacity(const PointRef&)>& resolveCap,
                          const std::function<Pos(const PointRef&)>& posOf) const override {
            PointRef partner;
            if      (p == a) { partner = b; }
            else if (p == b) { partner = a; }
            else             { return Capacity::free(); }
            if (resolveCap(partner).kind != Capacity::Locked) { return Capacity::free(); }
            if (resolveCap(refA).kind != Capacity::Locked ||
                resolveCap(refB).kind != Capacity::Locked) { return Capacity::free(); }   // length not pinned
            Pos radius = posOf(p) - posOf(partner);
            return Capacity::line(Pos(-radius.y, radius.x));   // tangent to the circle
        }

        // Match this segment's length to the reference's current length (read live),
        // correcting along its own direction like Distance. The reference defines the
        // length, so it is not adjusted here.
        void relax(MotionField& field, float rate) const override {
            float fa = field.freedom(a), fb = field.freedom(b);
            if (fa + fb <= 0.0f) { return; }
            float L = (field.pos(refB) - field.pos(refA)).pythag();
            Pos delta = field.pos(b) - field.pos(a);
            float len = delta.pythag();
            if (len < 1e-6f) { return; }
            Pos corr = delta / len * (len - L);
            field.nudge(a, corr * ( (fa / (fa + fb)) * rate));
            field.nudge(b, corr * (-(fb / (fa + fb)) * rate));
        }

        float residual(const std::function<Pos(const PointRef&)>& posOf) const override {
            float L = (posOf(refB) - posOf(refA)).pythag();
            return std::fabs((posOf(b) - posOf(a)).pythag() - L);
        }
    };

    // Factory: reconstruct a relation from its JSON envelope (by "kind").
    inline std::unique_ptr<Relation> relationFromJson(const Json& j) {

        const std::string kind = j.value("kind", std::string());
        if (kind.empty()) { return nullptr; }

        std::unique_ptr<Relation> r;
        if      (kind == "coincident") { r = std::make_unique<Coincident>(); }
        else if (kind == "lock")       { r = std::make_unique<Lock>(); }
        else if (kind == "distance")   { r = std::make_unique<Distance>(); }
        else if (kind == "parallel")   { r = std::make_unique<Parallel>(); }
        else if (kind == "equal")      { r = std::make_unique<Equal>(); }

        if (!r) { return nullptr; }

        r->load(j);
        r->id = idFromHex(j.value("id", std::string()));
        if (r->id == 0) { r->id = newId(); }
        return r;
    }

    // The full contents of a sketch: the polymorphic list of entities and the
    // relations between them. Used both as the project's committed geometry and as
    // a tool's transient preview.
    struct SketchGeometry {

        std::vector<std::unique_ptr<Stoicheion>> entities;
        std::vector<std::unique_ptr<Relation>>   relations;

        SketchGeometry() = default;
        SketchGeometry(SketchGeometry&&) = default;
        SketchGeometry& operator=(SketchGeometry&&) = default;
        SketchGeometry(const SketchGeometry&) = delete;
        SketchGeometry& operator=(const SketchGeometry&) = delete;

        void clear() { entities.clear(); relations.clear(); }
        bool empty() const { return entities.empty() && relations.empty(); }

        // Resolve an entity by its stable id (linear for now; a cached id->pointer
        // map comes later).
        Stoicheion* byId(Id id) const {
            for (const auto& e : entities) { if (e && e->id == id) { return e.get(); } }
            return nullptr;
        }

        // Current position of a referenced point (via anchors, which mirror the
        // control-point order). Empty if the point is gone.
        Pos posOf(const PointRef& p) const {
            Stoicheion* e = byId(p.entity);
            if (!e) { return Pos(); }
            std::vector<Pos> anc;
            e->anchors(anc);
            if (p.slot < 0 || p.slot >= static_cast<int>(anc.size())) { return Pos(); }
            return anc[static_cast<size_t>(p.slot)];
        }

        // The datum axis segment representing a world direction: the locked,
        // two-point entity lying on the X axis (both ys ~ 0) or the Y axis (both xs
        // ~ 0). Returns 0 if not found. Lets relations reference an axis' endpoints
        // as their direction representative.
        Id axisId(bool vertical) const {
            for (const auto& e : entities) {
                if (!e || !e->locked) { continue; }
                std::vector<Pos> anc;
                e->anchors(anc);
                if (anc.size() != 2) { continue; }
                bool match = vertical
                    ? (std::fabs(anc[0].x) < 1e-3f && std::fabs(anc[1].x) < 1e-3f)
                    : (std::fabs(anc[0].y) < 1e-3f && std::fabs(anc[1].y) < 1e-3f);
                if (match) { return e->id; }
            }
            return 0;
        }

        // The authority order (depth from the root) of a point: 0 for a grounded or
        // free root, otherwise one deeper than the deepest point that determines it.
        // Loops never form, so this is well-defined (with a defensive cycle guard).
        int orderOf(const PointRef& p) const {
            std::vector<PointRef> visiting;
            return orderImpl(p, visiting);
        }

        int orderImpl(const PointRef& p, std::vector<PointRef>& visiting) const {
            for (const PointRef& v : visiting) { if (v == p) { return 0; } }
            if (Stoicheion* e = byId(p.entity); e && e->locked) { return 0; }

            visiting.push_back(p);
            int best = -1;
            std::vector<PointRef> ms;
            for (const auto& r : relations) {
                if (!r || !(r->follower() == p)) { continue; }
                ms.clear();
                r->masters(ms);
                for (const PointRef& m : ms) { best = std::max(best, orderImpl(m, visiting)); }
            }
            visiting.pop_back();
            return (best < 0) ? 0 : best + 1;
        }

        // The prime nodes: the highest-authority point of every independent
        // authority tree (one per connected component) -- a point that nothing
        // determines (orderOf == 0), so it grounds (or heads) its tree. These are
        // the roots the solve starts from. Computed, so it is always accurate as
        // trees form and merge; not all trees need be connected to each other.
        std::vector<PointRef> primeNodes() const {
            std::vector<PointRef> participants;
            auto add = [&](const PointRef& p) {
                for (const PointRef& x : participants) { if (x == p) { return; } }
                participants.push_back(p);
            };
            for (const auto& r : relations) {
                if (!r) { continue; }
                add(r->follower());
                std::vector<PointRef> ms;
                r->masters(ms);
                for (const PointRef& m : ms) { add(m); }
            }

            std::vector<PointRef> roots;
            for (const PointRef& p : participants) {
                if (orderOf(p) == 0) { roots.push_back(p); }
            }
            return roots;
        }

        // Enforce every relation *symmetrically*. Relations relate, they do not drive:
        // there is no master->follower order in which one relation gets to reposition a
        // point and another is skipped as a "loop-closer" (which is what let a Parallel
        // silently break a Coincident sharing the same vertex). Instead every relation
        // is a constraint the relaxation solver satisfies together, each pass.
        //
        // The only true authorities are the ones whose value comes from *outside* the
        // system -- Lock (an absolute position) and, via the solver, Distance (a length).
        // We first re-assert the grounding Locks (their points are absolute), then let
        // the relaxation settle every relation at once. Points grounded back to a datum
        // stay put; a free-floating cluster settles into the nearest configuration that
        // honours all of its relations, moving bodily if nothing pins it.
        void resolveRelations() {
            auto lookup = [this](Id id) { return byId(id); };
            std::vector<PointRef> ms;
            for (const auto& r : relations) {
                if (!r) { continue; }
                ms.clear();
                r->masters(ms);
                if (ms.empty()) { r->apply(lookup); }   // grounding (Lock): absolute authority
            }
            relaxDrag({}, 200, 1.0f);                    // settle all relations together
            normalize();                                 // re-seat chirality markers canonically
        }

        // Re-seat every entity's chirality marker (circle b / arc d) to its canonical
        // spot. The markers ride along with moves but are never solver variables, so
        // this is run after any settle/drag to keep them at the proper location.
        void normalize() {
            for (const auto& e : entities) { if (e) { e->normalize(); } }
        }

        // The largest violation across all relations, in world units (0 = every
        // relation exactly satisfied). After a settle, a satisfiable system drives
        // this near zero; a genuinely over-constrained one leaves it large.
        float maxResidual() const {
            auto pos = [this](const PointRef& p) { return posOf(p); };
            float worst = 0.0f;
            for (const auto& r : relations) { if (r) { worst = std::max(worst, r->residual(pos)); } }
            return worst;
        }

        // A relation can coexist with the existing set if, once everything settles, no
        // relation is left meaningfully violated (a few thousandths of a unit of slack
        // for incomplete convergence).
        static constexpr float ConflictTolerance = 1.0f;

        // Propose a new relation. We add it (it is the most *junior* relation, since
        // order of creation is seniority) and settle. If the system can satisfy it
        // along with everything else, it stays. If it conflicts, the senior relations
        // win and the newcomer is withdrawn -- "the most senior dimension wins." Returns
        // whether the relation was accepted.
        bool proposeRelation(std::unique_ptr<Relation> r) {
            if (r->id == 0) { r->id = newId(); }
            relations.push_back(std::move(r));
            resolveRelations();
            if (maxResidual() <= ConflictTolerance) { return true; }
            relations.pop_back();                       // junior newcomer loses the conflict
            resolveRelations();                         // restore the prior configuration
            return false;
        }

        // The interactive ("annealing") drag solver. The user holds some points at
        // target positions; we distribute that motion through the relation graph by
        // relaxation. Only points grounded to a datum (pointSolved) are truly fixed --
        // so a *free-floating* cluster, anchored to nothing, moves bodily (even its
        // own prime root), exactly as it should when nothing pins it in place.
        //
        // It is a soft, position-based pass: it spreads motion to the least-solved
        // points "as if by friction". resolveRelations() drives the same engine with
        // no held points and more iterations to settle the graph after an edit.
        void relaxDrag(const std::vector<DragPoint>& held, int iterations = 24, float rate = 0.5f) {

            MotionField field;

            // Every anchor is a node; movable unless grounded to a datum (locked
            // entity, or solved through the authority chain) -- or unless the anchor
            // is not a writable control point (an ellipse arc's endpoints are derived
            // from its parameters, so they can only act as fixed coincidence anchors
            // until they are made settable; see note in the relax loop).
            for (const auto& e : entities) {
                if (!e) { continue; }
                std::vector<Pos> anc;
                e->anchors(anc);
                std::vector<Pos2*> cps;
                e->controlPoints(cps);
                for (size_t s = 0; s < anc.size(); s++) {
                    PointRef pr{ e->id, static_cast<int>(s) };
                    bool writable = s < cps.size();
                    bool grounded = e->locked || !writable || pointSolved(pr);
                    field.nodes.push_back({ pr, anc[s], !grounded });
                }
            }

            // The held points become pinned anchors at their target -- but a grounded
            // point cannot be dragged, so it stays where it is.
            for (const DragPoint& d : held) {
                int i = field.find(d.ref);
                if (i < 0) { continue; }
                if (field.nodes[i].movable) { field.nodes[i].pos = d.target; }
                field.nodes[i].movable = false;
            }

            // Gauss-Seidel relaxation. Each pass: every relation nudges its free
            // points; every entity re-asserts its own inherent invariants (so an
            // arc's endpoint can never leave its circle); then coincidence is welded
            // *exactly*. Ending each pass with the weld means the displayed result
            // always has rigid endpoint coincidence, with shapes conformed around it.
            for (int it = 0; it < iterations; it++) {
                for (const auto& r : relations) { if (r) { r->relax(field, rate); } }
                for (const auto& e : entities) { if (e) { e->relaxInherent(field, e->id, rate); } }
                weldCoincidences(field);
            }

            auto lookup = [this](Id id) { return byId(id); };
            for (const auto& n : field.nodes) { writePoint(lookup, n.ref, n.pos); }
        }

        // Strictly enforce coincidence in the working field: every coincidence cluster
        // is collapsed to a single position. A pinned member (grounded datum / held
        // point) is the consensus the rest snap onto; with none pinned, the cluster
        // meets at its average. This is the hard counterpart to the coincidence spring.
        void weldCoincidences(MotionField& field) const {
            std::vector<PointRef> done;
            auto seen = [&](const PointRef& p) {
                for (const PointRef& d : done) { if (d == p) { return true; } }
                return false;
            };
            for (const auto& r : relations) {
                if (!r) { continue; }
                PointRef ca, cb;
                if (!r->coincidentPair(ca, cb)) { continue; }
                if (seen(ca)) { continue; }

                std::vector<PointRef> cluster;
                coincidentClosure(ca, cluster);

                // Consensus: a pinned member's exact position if any, else the average.
                Pos consensus; bool pinnedFound = false; int count = 0;
                for (const PointRef& m : cluster) {
                    int i = field.find(m);
                    if (i < 0) { continue; }
                    if (!field.nodes[i].movable) { consensus = field.nodes[i].pos; pinnedFound = true; break; }
                    consensus = consensus + field.nodes[i].pos; count++;
                }
                if (!pinnedFound) {
                    if (count == 0) { continue; }
                    consensus = consensus / static_cast<float>(count);
                }
                for (const PointRef& m : cluster) { field.set(m, consensus); done.push_back(m); }
            }
        }

        // The coincident cluster of a point: every point tied to it (transitively)
        // by coincidence relations. These must move together under a drag.
        void coincidentClosure(const PointRef& seed, std::vector<PointRef>& out) const {
            out.push_back(seed);
            std::vector<PointRef> stack{ seed };
            while (!stack.empty()) {
                PointRef p = stack.back();
                stack.pop_back();
                for (const auto& r : relations) {
                    if (!r) { continue; }
                    PointRef ca, cb;
                    if (!r->coincidentPair(ca, cb)) { continue; }
                    PointRef other;
                    if      (ca == p) { other = cb; }
                    else if (cb == p) { other = ca; }
                    else              { continue; }
                    bool seen = false;
                    for (const PointRef& o : out) { if (o == other) { seen = true; break; } }
                    if (!seen) { out.push_back(other); stack.push_back(other); }
                }
            }
        }

        // The motion capacity of a point: the intersection of every relation's
        // restriction on it, resolved along the authority chain back to the root.
        Capacity capacityOf(const PointRef& p) const {
            std::vector<PointRef> visiting;
            return capacityImpl(p, visiting);
        }

        Capacity capacityImpl(const PointRef& p, std::vector<PointRef>& visiting) const {
            for (const PointRef& v : visiting) { if (v == p) { return Capacity::free(); } }  // cycle guard

            // A datum (origin / axes) is fixed -- its points have no freedom.
            if (Stoicheion* e = byId(p.entity); e && e->locked) { return Capacity::locked(); }

            visiting.push_back(p);
            auto resolveCap = [this, &visiting](const PointRef& q) { return capacityImpl(q, visiting); };
            auto pos        = [this](const PointRef& q) { return posOf(q); };
            Capacity cap = Capacity::free();
            for (const auto& r : relations) {
                if (r) { cap = Capacity::combine(cap, r->capacity(p, resolveCap, pos)); }
            }
            visiting.pop_back();
            return cap;
        }

        // A point is "solved" (fully constrained / determined) iff it has zero
        // freedom -- its capacity, grounded through the authority chain, is Locked.
        bool pointSolved(const PointRef& p) const {
            return capacityOf(p).kind == Capacity::Locked;
        }

        // An entity is solved iff every one of its defining points is solved. The
        // point count comes from anchors() (same order/count as controlPoints()).
        bool solved(const Stoicheion& e) const {
            std::vector<Pos> anc;
            e.anchors(anc);
            if (anc.empty()) { return false; }
            int marker = e.chiralitySlot();   // the chirality marker is not a solved point
            for (size_t s = 0; s < anc.size(); s++) {
                if (static_cast<int>(s) == marker) { continue; }
                if (!pointSolved({ e.id, static_cast<int>(s) })) { return false; }
            }
            return true;
        }

        // Append an entity, assigning it a stable id if it doesn't have one yet.
        // Returns the new index (so a tool can keep extending it).
        size_t add(std::unique_ptr<Stoicheion> e) {
            if (e->id == 0) { e->id = newId(); }
            entities.push_back(std::move(e));
            return entities.size() - 1;
        }

        size_t addRelation(std::unique_ptr<Relation> r) {
            if (r->id == 0) { r->id = newId(); }
            relations.push_back(std::move(r));
            return relations.size() - 1;
        }

        Json toJson() const {
            Json out;
            out["entities"]  = Json::array();
            out["relations"] = Json::array();
            for (const auto& e : entities)  { if (e) { out["entities"].push_back(e->toJson()); } }
            for (const auto& r : relations) { if (r) { out["relations"].push_back(r->toJson()); } }
            return out;
        }

        static SketchGeometry fromJson(const Json& j) {
            SketchGeometry g;
            if (auto it = j.find("entities"); it != j.end() && it->is_array()) {
                for (const Json& e : *it) {
                    if (auto ent = stoicheionFromJson(e)) { g.entities.push_back(std::move(ent)); }
                }
            }
            if (auto it = j.find("relations"); it != j.end() && it->is_array()) {
                for (const Json& e : *it) {
                    if (auto rel = relationFromJson(e)) { g.relations.push_back(std::move(rel)); }
                }
            }
            return g;
        }
    };
}
