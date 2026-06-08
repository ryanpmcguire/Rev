module;

#include <vector>
#include <memory>
#include <string>
#include <utility>
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

    // Wrap an angle delta into [0, TAU).
    inline float wrapTau(float x) { while (x < 0.0f) { x += TAU; } while (x >= TAU) { x -= TAU; } return x; }

    // Sampling helpers (write a polyline approximation of a curve into `out`).
    //--------------------------------------------------

    inline int spanSteps(float span) {
        return std::max(2, static_cast<int>(std::ceil(std::fabs(span) / (TAU / 6400.0f))));
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
    // Euclid's "Elements" is the Stoicheia; a single primitive is a Stoicheion.
    // Each concrete type stores its own data and knows how to: serialise itself,
    // draw itself as a polyline (tessellate), measure distance to a probe point,
    // expose its anchor (snap) points and its mutable control points, and project
    // a point onto its body. Selection / rendering / snapping / moving in the view
    // are then a single loop of virtual calls -- no per-type branching.
    // ===============================================================
    struct Stoicheion {

        bool construction = false;   // reference geometry, not real output

        virtual ~Stoicheion() = default;

        // Identity / serialisation
        virtual const char* kind() const = 0;
        virtual std::unique_ptr<Stoicheion> clone() const = 0;
        virtual Json data() const = 0;          // type-specific payload
        virtual void load(const Json& j) = 0;   // read type-specific payload

        Json toJson() const {
            Json j = data();
            j["kind"] = kind();
            j["construction"] = construction;
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
            int tier = construction ? 0 : 1;
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
    };

    // Circle
    //--------------------------------------------------
    struct Circle2 : public Stoicheion {

        Pos2 c;
        float r = 0.0f;

        Circle2() = default;
        Circle2(const Pos& c, float r) : c(c), r(r) {}

        const char* kind() const override { return "circle"; }
        std::unique_ptr<Stoicheion> clone() const override { return std::make_unique<Circle2>(*this); }
        Json data() const override { return Json{ { "c", c.toJson() }, { "r", r } }; }
        void load(const Json& j) override {
            c = Pos2::fromJson(j.value("c", Json::object()));
            r = j.value("r", 0.0f);
        }

        void tessellate(std::vector<Pos>& out) const override { sampleArc(out, c, r, 0.0f, TAU, spanSteps(TAU)); }
        float distanceTo(Pos p) const override { return std::fabs((p - c).pythag() - r); }
        void anchors(std::vector<Pos>& out) const override { out.push_back(c); }
        void snapPoints(std::vector<Pos>& out) const override {
            out.push_back(c);                                   // centre + four quadrants
            out.push_back(c + Pos(r, 0.0f)); out.push_back(c + Pos(-r, 0.0f));
            out.push_back(c + Pos(0.0f, r)); out.push_back(c + Pos(0.0f, -r));
        }
        bool asCircle(Pos& cc, float& rr, float& a0, float& sweep) const override {
            cc = c; rr = r; a0 = 0.0f; sweep = TAU; return true;
        }
        bool footOnCurve(Pos p, Pos& out) const override {
            Pos d = p - c; float dd = d.pythag();
            out = (dd > 1e-6f) ? (c + d / dd * r) : (c + Pos(r, 0.0f));
            return true;
        }
        void controlPoints(std::vector<Pos2*>& out) override { out.push_back(&c); }
    };

    // Arc (always swept counterclockwise from A to B; order encodes which arc)
    //--------------------------------------------------
    struct Arc2 : public Stoicheion {

        Pos2 c, a, b;

        Arc2() = default;
        Arc2(const Pos& c, const Pos& a, const Pos& b) : c(c), a(a), b(b) {}

        float radius() const { return (a - c).pythag(); }
        float span() const { return wrapTau((b - c).angle() - (a - c).angle()); }   // CCW [0, TAU)

        const char* kind() const override { return "arc"; }
        std::unique_ptr<Stoicheion> clone() const override { return std::make_unique<Arc2>(*this); }
        Json data() const override { return Json{ { "c", c.toJson() }, { "a", a.toJson() }, { "b", b.toJson() } }; }
        void load(const Json& j) override {
            c = Pos2::fromJson(j.value("c", Json::object()));
            a = Pos2::fromJson(j.value("a", Json::object()));
            b = Pos2::fromJson(j.value("b", Json::object()));
        }

        void tessellate(std::vector<Pos>& out) const override {
            float r = radius(); if (r <= 0.0f) { return; }
            float sp = span();
            sampleArc(out, c, r, (a - c).angle(), sp, spanSteps(sp));
        }
        float distanceTo(Pos p) const override {
            float r = radius();
            if (r <= 0.0f) { return (p - a).pythag(); }
            float aP = wrapTau((p - c).angle() - (a - c).angle());
            if (aP <= span()) { return std::fabs((p - c).pythag() - r); }
            return std::min((p - a).pythag(), (p - b).pythag());
        }
        void anchors(std::vector<Pos>& out) const override {
            out.push_back(c); out.push_back(a); out.push_back(b);
        }
        void snapPoints(std::vector<Pos>& out) const override {
            out.push_back(c); out.push_back(a); out.push_back(b);
            float r = radius();
            if (r > 0.0f) { out.push_back(c + Pos::fromAngle((a - c).angle() + span() * 0.5f) * r); }  // arc midpoint
        }
        bool asCircle(Pos& cc, float& rr, float& a0, float& sweep) const override {
            float r = radius();
            if (r <= 0.0f) { return false; }
            cc = c; rr = r; a0 = (a - c).angle(); sweep = span(); return true;
        }
        bool footOnCurve(Pos p, Pos& out) const override {
            float r = radius(); if (r <= 0.0f) { return false; }
            float aP = wrapTau((p - c).angle() - (a - c).angle());
            if (aP > span()) { return false; }
            Pos d = p - c; float dd = d.pythag();
            out = (dd < 1e-6f) ? Pos(a) : (c + d / dd * r);
            return true;
        }
        void controlPoints(std::vector<Pos2*>& out) override { out.push_back(&c); out.push_back(&a); out.push_back(&b); }
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
        e->construction = j.value("construction", false);
        return e;
    }

    // The full contents of a sketch: one polymorphic list of entities. Used both as
    // the project's committed geometry and as a tool's transient preview.
    struct SketchGeometry {

        std::vector<std::unique_ptr<Stoicheion>> entities;

        SketchGeometry() = default;
        SketchGeometry(SketchGeometry&&) = default;
        SketchGeometry& operator=(SketchGeometry&&) = default;
        SketchGeometry(const SketchGeometry&) = delete;
        SketchGeometry& operator=(const SketchGeometry&) = delete;

        void clear() { entities.clear(); }
        bool empty() const { return entities.empty(); }

        // Append, returning the new index (so a tool can keep extending it).
        size_t add(std::unique_ptr<Stoicheion> e) {
            entities.push_back(std::move(e));
            return entities.size() - 1;
        }

        Json toJson() const {
            Json out;
            out["entities"] = Json::array();
            for (const auto& e : entities) { if (e) { out["entities"].push_back(e->toJson()); } }
            return out;
        }

        static SketchGeometry fromJson(const Json& j) {
            SketchGeometry g;
            if (auto it = j.find("entities"); it != j.end() && it->is_array()) {
                for (const Json& e : *it) {
                    if (auto ent = stoicheionFromJson(e)) { g.entities.push_back(std::move(ent)); }
                }
            }
            return g;
        }
    };
}
