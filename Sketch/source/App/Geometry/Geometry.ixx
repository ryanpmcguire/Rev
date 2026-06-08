module;

#include <vector>
#include <memory>
#include <string>
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

        // A salient point: an anchor for snapping and point-priority selection.
        // `sub`/`vertex` address a part within a composite entity (polylines).
        struct Anchor { Pos pos; int sub = -1; bool vertex = false; };

        // Rendering: append this entity's polyline approximation (world space).
        // Point-like entities append a single position, drawn as a dot.
        virtual void tessellate(std::vector<Pos>& out) const = 0;
        virtual bool isPoint() const { return false; }

        // Render just one part (a polyline vertex/segment); defaults to the whole.
        virtual void tessellatePart(int sub, bool vertex, std::vector<Pos>& out) const {
            tessellate(out);
        }

        // Hit-testing: distance from p to the drawn curve. `nearestSub` receives the
        // nearest sub-part index for composite entities (else -1).
        virtual float distanceTo(Pos p, int& nearestSub) const = 0;

        // Snap / point-priority anchors (endpoints, centre, vertices).
        virtual void anchors(std::vector<Anchor>& out) const {}

        // Nearest point on the body for snapping onto the curve; false if none.
        virtual bool footOnCurve(Pos p, Pos& out) const { return false; }

        // Composite sub-structure (polylines): number of selectable sub-segments
        // (0 = atomic), and vertex removal (returns whether the entity is still
        // valid afterwards). Atomic entities ignore both.
        virtual int subCount() const { return 0; }
        virtual bool eraseVertex(int v) { return false; }

        // Mutable control points. The no-arg form yields every absolute point (for
        // coincidence detection); the part form yields just the points a given
        // sub-part move should translate (defaults to all).
        virtual void controlPoints(std::vector<Pos2*>& out) = 0;
        virtual void controlPoints(int sub, bool vertex, std::vector<Pos2*>& out) {
            controlPoints(out);
        }
    };

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
        float distanceTo(Pos q, int& sub) const override { sub = -1; return (q - p).pythag(); }
        void anchors(std::vector<Anchor>& out) const override { out.push_back({ p, -1, false }); }
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
        float distanceTo(Pos p, int& sub) const override { sub = -1; return (p - closestOnSegment(p, a, b)).pythag(); }
        void anchors(std::vector<Anchor>& out) const override { out.push_back({ a, -1, false }); out.push_back({ b, -1, false }); }
        bool footOnCurve(Pos p, Pos& out) const override { out = closestOnSegment(p, a, b); return true; }
        void controlPoints(std::vector<Pos2*>& out) override { out.push_back(&a); out.push_back(&b); }
    };

    // Polyline (a connected chain; interior joints are shared, movable vertices)
    //--------------------------------------------------
    struct Polyline2 : public Stoicheion {

        std::vector<Pos2> points;

        const char* kind() const override { return "polyline"; }
        std::unique_ptr<Stoicheion> clone() const override { return std::make_unique<Polyline2>(*this); }
        Json data() const override {
            Json arr = Json::array();
            for (const Pos2& p : points) { arr.push_back(p.toJson()); }
            return Json{ { "points", arr } };
        }
        void load(const Json& j) override {
            points.clear();
            if (auto it = j.find("points"); it != j.end() && it->is_array()) {
                for (const Json& e : *it) { points.push_back(Pos2::fromJson(e)); }
            }
        }

        void tessellate(std::vector<Pos>& out) const override {
            for (const Pos2& p : points) { out.push_back(p); }
        }
        void tessellatePart(int sub, bool vertex, std::vector<Pos>& out) const override {
            if (vertex) {
                if (sub >= 0 && sub < static_cast<int>(points.size())) { out.push_back(points[sub]); }
            }
            else if (sub >= 0 && sub + 1 < static_cast<int>(points.size())) {
                out.push_back(points[sub]); out.push_back(points[sub + 1]);
            }
            else { tessellate(out); }
        }

        float distanceTo(Pos p, int& nearestSub) const override {
            nearestSub = -1;
            float best = 1e30f;
            for (size_t s = 0; s + 1 < points.size(); s++) {
                float d = (p - closestOnSegment(p, points[s], points[s + 1])).pythag();
                if (d < best) { best = d; nearestSub = static_cast<int>(s); }
            }
            return best;
        }
        void anchors(std::vector<Anchor>& out) const override {
            for (size_t i = 0; i < points.size(); i++) { out.push_back({ points[i], static_cast<int>(i), true }); }
        }
        bool footOnCurve(Pos p, Pos& out) const override {
            int sub; if (points.size() < 2) { return false; }
            distanceTo(p, sub);
            if (sub < 0) { return false; }
            out = closestOnSegment(p, points[sub], points[sub + 1]);
            return true;
        }
        int subCount() const override { return points.size() > 1 ? static_cast<int>(points.size()) - 1 : 0; }
        bool eraseVertex(int v) override {
            if (v >= 0 && v < static_cast<int>(points.size())) { points.erase(points.begin() + v); }
            return points.size() >= 2;
        }
        void controlPoints(std::vector<Pos2*>& out) override {
            for (Pos2& p : points) { out.push_back(&p); }
        }
        void controlPoints(int sub, bool vertex, std::vector<Pos2*>& out) override {
            if (vertex && sub >= 0 && sub < static_cast<int>(points.size())) {
                out.push_back(&points[sub]);
            }
            else if (!vertex && sub >= 0 && sub + 1 < static_cast<int>(points.size())) {
                out.push_back(&points[sub]); out.push_back(&points[sub + 1]);
            }
            else { controlPoints(out); }
        }
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
        float distanceTo(Pos p, int& sub) const override { sub = -1; return std::fabs((p - c).pythag() - r); }
        void anchors(std::vector<Anchor>& out) const override { out.push_back({ c, -1, false }); }
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
        float distanceTo(Pos p, int& sub) const override {
            sub = -1;
            float r = radius();
            if (r <= 0.0f) { return (p - a).pythag(); }
            float aP = wrapTau((p - c).angle() - (a - c).angle());
            if (aP <= span()) { return std::fabs((p - c).pythag() - r); }
            return std::min((p - a).pythag(), (p - b).pythag());
        }
        void anchors(std::vector<Anchor>& out) const override {
            out.push_back({ c, -1, false }); out.push_back({ a, -1, false }); out.push_back({ b, -1, false });
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
        float distanceTo(Pos p, int& sub) const override {
            sub = -1; Pos foot; footNearest(p, 0.0f, TAU, foot); return (p - foot).pythag();
        }
        void anchors(std::vector<Anchor>& out) const override { out.push_back({ c, -1, false }); }
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
        float distanceTo(Pos p, int& sub) const override {
            sub = -1; Pos foot; footNearest(p, foot); return (p - foot).pythag();
        }
        void anchors(std::vector<Anchor>& out) const override {
            out.push_back({ c, -1, false });
            out.push_back({ startPoint(), -1, false });
            out.push_back({ endPoint(), -1, false });
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
        else if (kind == "polyline")   { e = std::make_unique<Polyline2>(); }
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
