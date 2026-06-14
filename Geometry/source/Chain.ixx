module;

#include <vector>
#include <memory>
#include <string>
#include <cmath>
#include <algorithm>
#include <utility>
#include <functional>

export module Geo.Chain;

import Rev.Core.Pos;

export import Geo.Geometry;

// =====================================================================
// Chain -- an ordered run of stoicheia (segments / arcs) connected end to end,
// the unit of 2D toolpath offsetting.
//
// Offsetting is BLIND and uniform: every edge slides to the left of its own
// travel direction, and every corner -- converging or diverging alike -- is
// bridged by the join arc known by construction (centred on the original corner,
// radius = the offset distance, continuing the first edge's travel). No winding
// test, no preferred side, no mitre/clip decision: inset vs outset emerges purely
// from the chain's own chirality, and the raw result's self-crossings are the
// raw material for the chain extraction step that follows.
// =====================================================================
export namespace Geo {

    using Rev::Core::Pos;

    // dPos -- double-precision computation point. Stored geometry stays float
    // (Pos); the chain's numerical KERNELS run in double, converting at the
    // boundaries. The failures double prevents are catastrophic cancellations:
    // a nearly-complete corner arc subtracts two nearly-equal angles, a near-miss
    // intersection divides by a nearly-vanishing cross product -- float's ~7
    // digits get eaten whole, double's ~16 leave plenty.
    struct dPos {
        double x = 0.0, y = 0.0;
        dPos() = default;
        dPos(double x, double y) : x(x), y(y) {}
        dPos(const Pos& p) : x(p.x), y(p.y) {}

        Pos f() const { return Pos(static_cast<float>(x), static_cast<float>(y)); }

        dPos operator+(dPos o) const { return { x + o.x, y + o.y }; }
        dPos operator-(dPos o) const { return { x - o.x, y - o.y }; }
        dPos operator*(double k) const { return { x * k, y * k }; }
        dPos operator/(double k) const { return { x / k, y / k }; }

        double dot(dPos o) const { return x * o.x + y * o.y; }
        double cross(dPos o) const { return x * o.y - y * o.x; }
        double len() const { return std::sqrt(x * x + y * y); }
        double angle() const { return std::atan2(y, x); }
        dPos perp() const { return { -y, x }; }                       // +90 deg
        dPos unit() const { double l = len(); return (l > 0.0) ? dPos(x / l, y / l) : dPos(); }
        static dPos fromAngle(double a) { return { std::cos(a), std::sin(a) }; }
    };

    inline constexpr double dTAU = 6.283185307179586476925287;
    inline double dWrap(double a) { while (a < 0.0) { a += dTAU; } while (a >= dTAU) { a -= dTAU; } return a; }

    // What a chain IS to the tool, beyond its geometry: a cutting move, or one
    // of the two link kinds a strategy weaves between cutting chains. Semantic,
    // not cosmetic -- the CAM app decides retract heights from it; the sketch
    // app merely picks colours.
    enum class LinkKind : std::uint8_t {
        None,      // an ordinary cutting chain
        Cut,       // cutting link: tool stays down, hops to the next concentric ring
        Retract,   // retract link: tool must fully retract before the next branch
        LeadIn,    // lead-in: a ramp that eases ONTO a cut, woven after the retract
                   // link and before the cut (the consumer descends it into the cut)
        LeadOut,   // lead-out: the mirror, easing OFF the cut before the retract lifts
                   // (the consumer climbs it back out of the cut)
        Finish     // a CUTTING chain like None, but flagged as the thin finishing pass
                   // (a small inset taken just after the boundary-clearance ring) so a
                   // consumer can give it its own feed / speed
    };

    struct Chain {

        std::vector<std::unique_ptr<Stoicheion>> edges;   // oriented: travel is start -> end
        bool closed = false;

        // Ancestry: every chain carries a unique id, and a reference to the chain
        // it descended from (0 = a root, e.g. profile 0). Children reference
        // parents -- never the other way (no child lists) -- so the ancestral
        // tree is implicit in the chains themselves, and a later linking pass can
        // walk profiles depth-first by following parents upward.
        Id id = 0;
        Id parent = 0;

        // Link tag (toolpath semantics; LinkKind::None for cutting chains).
        LinkKind link = LinkKind::None;

        Chain() = default;
        Chain(Chain&&) = default;
        Chain& operator=(Chain&&) = default;

        Chain clone() const {
            Chain c; c.closed = closed;
            c.id = id; c.parent = parent; c.link = link;
            for (const auto& e : edges) { c.edges.push_back(e->clone()); }
            return c;
        }

        // This chain travelled the opposite way: edge order flipped, every edge
        // reversed (flags preserved). For conditioning inputs whose winding is
        // arbitrary (e.g. mesh sections) to the doctrine orientation at the door
        // -- never for reorienting the method's own outputs.
        Chain reversed() const {
            Chain r; r.closed = closed;
            r.id = id; r.parent = parent; r.link = link;
            for (size_t i = edges.size(); i-- > 0; ) { r.edges.push_back(reversedEdge(*edges[i])); }
            return r;
        }

        // Type probes / accessors. The chain holds segments, arcs and full circles
        // (ellipses are tessellated to segments at build time).
        static bool isArc(const Stoicheion& e)    { return e.type() == SKind::Arc; }
        static bool isCircle(const Stoicheion& e) { return e.type() == SKind::Circle; }
        static bool isOffEll(const Stoicheion& e) { return e.type() == SKind::OffsetEllipse; }
        static const Segment2* asSeg(const Stoicheion& e)  { return static_cast<const Segment2*>(&e); }
        static const Arc2*     asArc(const Stoicheion& e)  { return static_cast<const Arc2*>(&e); }
        static const Circle2*  asCirc(const Stoicheion& e) { return static_cast<const Circle2*>(&e); }
        static const OffsetEllipse2* asOff(const Stoicheion& e) { return static_cast<const OffsetEllipse2*>(&e); }

        // Certified root finding over the normalised span s in [0,1]: a uniform
        // scan brackets every sign change, bisection drives each bracket to
        // machine precision. This is the "recursive approximation" the elliptic
        // cases run on -- deterministic, every transversal root found, exact to
        // double epsilon -- replacing the closed-form radicals that provably do
        // not exist for parallel curves of ellipses.
        static void scanRoots(const std::function<double(double)>& g, int N, std::vector<double>& out) {
            double prevS = 0.0, prevG = g(0.0);
            for (int i = 1; i <= N; i++) {
                double s = static_cast<double>(i) / N;
                double gi = g(s);
                if ((prevG < 0.0) != (gi < 0.0)) {
                    double lo = prevS, hi = s, glo = prevG;
                    for (int it = 0; it < 60; it++) {
                        double mid = 0.5 * (lo + hi), gm = g(mid);
                        if ((glo < 0.0) != (gm < 0.0)) { hi = mid; } else { lo = mid; glo = gm; }
                    }
                    out.push_back(0.5 * (lo + hi));
                }
                prevS = s; prevG = gi;
            }
        }

        // Travel coordinate s in [0,1] of a point on (or near) an offset ellipse:
        // coarse scan for the nearest sample, then ternary refinement.
        static float oeParamOf(const OffsetEllipse2& oe, Pos p) {
            const int N = 128;
            double best = 1e30; double bs = 0.0;
            for (int i = 0; i <= N; i++) {
                double s = static_cast<double>(i) / N;
                double dist = (dPos(oe.pointAt(static_cast<float>(s))) - dPos(p)).len();
                if (dist < best) { best = dist; bs = s; }
            }
            double lo = std::max(0.0, bs - 1.0 / N), hi = std::min(1.0, bs + 1.0 / N);
            for (int it = 0; it < 50; it++) {
                double m1 = lo + (hi - lo) / 3.0, m2 = hi - (hi - lo) / 3.0;
                double d1 = (dPos(oe.pointAt(static_cast<float>(m1))) - dPos(p)).len();
                double d2 = (dPos(oe.pointAt(static_cast<float>(m2))) - dPos(p)).len();
                if (d1 < d2) { hi = m2; } else { lo = m1; }
            }
            return static_cast<float>(0.5 * (lo + hi));
        }
        static Segment2* asSeg(Stoicheion& e) { return static_cast<Segment2*>(&e); }
        static Arc2*     asArc(Stoicheion& e) { return static_cast<Arc2*>(&e); }

        // The circular probe: an arc or a full circle as (centre, radius, CCW span
        // start + sweep, travel chirality). One abstraction so every consumer below
        // treats arcs exactly like lines treat lines -- the chirality (an arc's
        // through-point d, a circle's marker b) always rides along.
        static bool circularOf(const Stoicheion& e, Pos& c, float& r, float& a0, float& sweep, int& chir) {
            if (isArc(e)) {
                const Arc2* a = asArc(e);
                r = a->radius(); if (r <= 0.0f) { return false; }
                c = a->c; a->range(a0, sweep); chir = a->chirality();
                return true;
            }
            if (isCircle(e)) {
                const Circle2* k = asCirc(e);
                r = k->radius(); if (r <= 0.0f) { return false; }
                c = k->c; a0 = (k->a - k->c).angle(); sweep = TAU; chir = k->chirality();
                return true;
            }
            return false;
        }

        static Pos perpCCW(Pos v) { return Pos(-v.y, v.x); }   // +90 deg rotation

        static Pos eStart(const Stoicheion& e) {
            if (isArc(e)) { return Pos(asArc(e)->a); }
            if (isCircle(e)) { return Pos(asCirc(e)->a); }   // a closed loop: start == end == a
            if (isOffEll(e)) { return asOff(e)->pointAt(0.0f); }
            return Pos(asSeg(e)->a);
        }
        static Pos eEnd(const Stoicheion& e) {
            if (isArc(e)) { return Pos(asArc(e)->b); }
            if (isCircle(e)) { return Pos(asCirc(e)->a); }
            if (isOffEll(e)) { return asOff(e)->pointAt(1.0f); }
            return Pos(asSeg(e)->b);
        }

        // Unit tangent in the direction of travel, at the start / end.
        static Pos eStartDir(const Stoicheion& e) {
            Pos c; float r, a0, sweep; int chir;
            if (circularOf(e, c, r, a0, sweep, chir)) {
                Pos t = perpCCW(eStart(e) - c);
                if (chir < 0) { t = t * -1.0f; }
                return t.normalized();
            }
            if (isOffEll(e)) { return asOff(e)->tangentAt(0.0f); }
            const Segment2* s = asSeg(e);
            return (s->b - s->a).normalized();
        }
        static Pos eEndDir(const Stoicheion& e) {
            Pos c; float r, a0, sweep; int chir;
            if (circularOf(e, c, r, a0, sweep, chir)) {
                Pos t = perpCCW(eEnd(e) - c);
                if (chir < 0) { t = t * -1.0f; }
                return t.normalized();
            }
            if (isOffEll(e)) { return asOff(e)->tangentAt(1.0f); }
            const Segment2* s = asSeg(e);
            return (s->b - s->a).normalized();
        }

        // Reverse an edge's travel direction (so chain building can connect either
        // end). Reversal changes the GEOMETRY's orientation only -- every carried
        // property (open-air, construction, group, id) survives the flip.
        static std::unique_ptr<Stoicheion> reversedEdge(const Stoicheion& e) {
            std::unique_ptr<Stoicheion> r;
            if (isArc(e)) { const Arc2* a = asArc(e); r = std::make_unique<Arc2>(a->c, a->b, a->a, a->d); }
            else if (isOffEll(e)) {
                const OffsetEllipse2* o = asOff(e);   // travel is the parameter order: just swap
                r = std::make_unique<OffsetEllipse2>(o->c, o->u, o->v, o->d, o->a1, o->a0);
            }
            else {
                const Segment2* s = asSeg(e);
                r = std::make_unique<Segment2>(s->b, s->a);
            }
            r->id = e.id;
            r->construction = e.construction;
            r->openAir = e.openAir;
            r->group = e.group;
            r->groupLevel = e.groupLevel;
            return r;
        }

        // Offset an edge to the LEFT of travel by `d`. A line stays a parallel line;
        // an arc / circle stays concentric (left of CCW travel is inward). The
        // chirality carrier (an arc's through-point d, a circle's marker b) is scaled
        // along its own radial, so it stays on its side: orientation is preserved,
        // never recomputed.
        static std::unique_ptr<Stoicheion> offsetLeft(const Stoicheion& e, float d) {
            if (isArc(e)) {
                const Arc2* a = asArc(e);
                float r = a->radius();
                if (r < 1e-9f) { return e.clone(); }
                float nr = r - d * static_cast<float>(a->chirality());   // left = inward for CCW
                // Through the singularity (nr < 0) the arc re-emerges on the far
                // side with its chirality FLIPPED -- the behaviour a segment spline
                // of the arc exhibits naturally. Endpoints scale by nr (negative =
                // their true antipodal parallel points); the through-point scales by
                // |nr|, KEEPING its original bearing -- which lands it on the
                // complementary side of the chord and flips the travel sense.
                Pos na = a->c + (a->a - a->c) / r * nr;
                Pos nb = a->c + (a->b - a->c) / r * nr;
                float ld = (a->d - a->c).pythag();
                Pos nd = (ld > 1e-9f) ? (a->c + (a->d - a->c) / ld * std::fabs(nr)) : Pos(a->d);
                return std::make_unique<Arc2>(a->c, na, nb, nd);
            }
            if (isCircle(e)) {
                const Circle2* k = asCirc(e);
                float r = k->radius();
                if (r < 1e-9f) { return e.clone(); }
                float nr = r - d * static_cast<float>(k->chirality());   // left = inward for CCW
                // Same flip-through-zero rule: the radius point antipodes (scale by
                // nr), the chirality marker keeps its bearing (scale by |nr|) -- a
                // circle driven through its centre turns inside out.
                Pos na = k->c + (k->a - k->c) / r * nr;
                float lb = (k->b - k->c).pythag();
                Pos nb = (lb > 1e-9f) ? (k->c + (k->b - k->c) / lb * std::fabs(nr)) : Pos(k->b);
                return std::make_unique<Circle2>(k->c, na, nb);
            }
            if (isOffEll(e)) {
                // The family is closed under offsetting: only `d` moves. Spatial
                // travel chirality = parameter direction combined with the base's
                // handedness; left of CCW travel is inward (less outward d).
                const OffsetEllipse2* o = asOff(e);
                int s = ((o->u.cross(o->v) >= 0.0f) == (o->a1 >= o->a0)) ? 1 : -1;
                return std::make_unique<OffsetEllipse2>(o->c, o->u, o->v,
                                                        o->d - d * static_cast<float>(s), o->a0, o->a1);
            }
            const Segment2* s = asSeg(e);
            Pos dir = s->b - s->a; float L = dir.pythag();
            if (L < 1e-9f) { return e.clone(); }
            Pos n = perpCCW(dir / L);                                   // left normal
            return std::make_unique<Segment2>(s->a + n * d, s->b + n * d);
        }

        // Infinite-support intersection primitives (extended line / full circle),
        // shared by the exact edge-crossing test below. All computed in DOUBLE:
        // these are the cancellation-prone kernels (near-parallel cross products,
        // grazing-circle discriminants).
        static bool lineLineInf(Pos a0, Pos a1, Pos b0, Pos b1, Pos& out) {
            dPos A0(a0), B0(b0);
            dPos r = dPos(a1) - A0, s = dPos(b1) - B0;
            double rxs = r.cross(s);
            if (std::fabs(rxs) <= 1e-12 * r.len() * s.len()) { return false; }   // parallel, RELATIVE test
            out = (A0 + r * ((B0 - A0).cross(s) / rxs)).f();
            return true;
        }
        static void lineCircleInf(Pos a0, Pos a1, Pos c, float rad, std::vector<Pos>& out) {
            dPos A0(a0);
            dPos d = dPos(a1) - A0, fc = A0 - dPos(c);
            double A = d.dot(d); if (A < 1e-18) { return; }
            double B = 2.0 * fc.dot(d), C = fc.dot(fc) - static_cast<double>(rad) * rad;
            double disc = B * B - 4.0 * A * C; if (disc < 0.0) { return; }
            double sq = std::sqrt(disc);
            // Numerically stable roots: never subtract nearly-equal quantities.
            double q = -0.5 * (B + (B >= 0.0 ? sq : -sq));
            double t1 = q / A;
            out.push_back((A0 + d * t1).f());
            if (sq > 0.0 && std::fabs(q) > 1e-300) {
                double t2 = C / q;
                if (t2 != t1) { out.push_back((A0 + d * t2).f()); }
            }
        }
        static void circleCircle(Pos c0, float r0, Pos c1, float r1, std::vector<Pos>& out) {
            dPos C0(c0), C1(c1);
            double R0 = r0, R1 = r1;
            dPos d = C1 - C0; double dist = d.len();
            if (dist < 1e-12 || dist > R0 + R1 + 1e-6 || dist < std::fabs(R0 - R1) - 1e-6) { return; }
            double a = (R0 * R0 - R1 * R1 + dist * dist) / (2.0 * dist);
            double h2 = R0 * R0 - a * a; if (h2 < 0.0) { h2 = 0.0; }
            double h = std::sqrt(h2);
            dPos mid = C0 + d * (a / dist);
            dPos perp = d.perp() * (h / dist);
            out.push_back((mid + perp).f());
            if (h > 1e-12) { out.push_back((mid - perp).f()); }
        }
        // Numerical hygiene for the blind offset, applied while CONSTRUCTING the
        // raw curve, before the crossing walk ever sees it. Real STEP files
        // routinely section into systems of PRACTICALLY TANGENT spans (a cylinder
        // arriving as several mutually tangent arcs), and offsetting such seams
        // must never manufacture degenerate join loops.
        //
        //   * fuseCloseTangencies: at a seam whose corner angle is nearly zero
        //     (near-exact tangency), no join arc is constructed -- the two offset
        //     endpoints are simply FUSED to their average (a sliver join loop
        //     there is pure numerical poison, and the fused error is sub-micron
        //     at real part scales).
        //
        //   * pruneSmallFeatures: an offset edge that has collapsed to (near)
        //     nothing -- e.g. an arc of radius 0.5 inset by 0.5 -- is dropped,
        //     and its neighbours join directly.
        //
        static constexpr bool fuseCloseTangencies = true;
        static constexpr bool pruneSmallFeatures = true;
        static constexpr float FuseSin = 1e-3f;        // sin of the seam angle below which we fuse
        static constexpr float PruneLength = 1e-3f;    // edges shorter than this are degenerate

        // Move an offset edge's travel endpoint (for fusing seams). An arc
        // re-seats the endpoint on its own circle; an offset ellipse cannot (its
        // endpoints are parameter-defined) and reports false.
        static bool setEdgeStart(Stoicheion& e, Pos p) {
            if (isArc(e)) {
                Arc2* a = static_cast<Arc2*>(&e);
                float r = a->radius();
                if (r > 1e-9f) { a->a = a->c + (p - a->c).normalized() * r; }
                return true;
            }
            if (isCircle(e) || isOffEll(e)) { return false; }
            static_cast<Segment2*>(&e)->a = p;
            return true;
        }
        static bool setEdgeEnd(Stoicheion& e, Pos p) {
            if (isArc(e)) {
                Arc2* a = static_cast<Arc2*>(&e);
                float r = a->radius();
                if (r > 1e-9f) { a->b = a->c + (p - a->c).normalized() * r; }
                return true;
            }
            if (isCircle(e) || isOffEll(e)) { return false; }
            static_cast<Segment2*>(&e)->b = p;
            return true;
        }

        // The *raw* offset, done BLINDLY: every edge slides to the LEFT of its own
        // travel direction by `amount` -- no winding test, no preferred side. The
        // chain's chirality alone decides what that means: a CCW loop's interior is
        // on the left, so a positive amount insets it; a CW loop's interior is on the
        // right, so the very same operation offsets it outward. Inset vs outset is
        // emergent, never inferred. The result may self-intersect; those crossings
        // are the raw material for the chain extraction to come.
        Chain offsetRaw(float amount) const {
            Chain result;
            if (edges.empty()) { return result; }
            result.closed = closed;

            // A lone full circle is its own closed loop: no corners, just the blind
            // concentric left-offset (its own chirality decides inset vs outset).
            if (edges.size() == 1 && isCircle(*edges.front())) {
                std::unique_ptr<Stoicheion> o = offsetLeft(*edges.front(), amount);
                if (pruneSmallFeatures && edgeLength(*o) < PruneLength) { return result; }   // extinct
                result.edges.push_back(std::move(o));
                return result;
            }

            // A lone full-span offset ellipse likewise: closed, no corners.
            if (edges.size() == 1 && isOffEll(*edges.front()) && asOff(*edges.front())->fullLoop()) {
                std::unique_ptr<Stoicheion> o = offsetLeft(*edges.front(), amount);
                if (pruneSmallFeatures && edgeLength(*o) < PruneLength) { return result; }   // extinct
                result.edges.push_back(std::move(o));
                return result;
            }

            // Guard: a chain holding anything we don't offset passes through.
            for (const auto& e : edges) {
                SKind t = e->type();
                if (t != SKind::Segment && t != SKind::Arc && t != SKind::OffsetEllipse) {
                    for (const auto& src : edges) { result.edges.push_back(src->clone()); }
                    return result;
                }
            }

            std::vector<std::unique_ptr<Stoicheion>> off;
            std::vector<size_t> src;                   // off[i] came from edges[src[i]]
            off.reserve(edges.size());
            src.reserve(edges.size());
            for (size_t i = 0; i < edges.size(); i++) {
                std::unique_ptr<Stoicheion> o = offsetLeft(*edges[i], amount);

                // pruneSmallFeatures: an edge that collapsed to (near) nothing
                // under the offset generates no geometry -- its neighbours will
                // join directly across the hole it leaves.
                if (pruneSmallFeatures && edgeLength(*o) < PruneLength) { continue; }

                off.push_back(std::move(o));
                src.push_back(i);
            }

            const size_t n = off.size();
            if (n == 0) { return result; }
            const size_t corners = closed ? n : (n > 0 ? n - 1 : 0);

            // The uniform construction: at EVERY corner insert the arc that *continues*
            // edge A's direction, loops around, and comes back along edge B's direction.
            // No mitre/clip decision, no trimming, no fallback. The join is known by
            // construction -- centred on the original (pre-offset) corner, radius
            // |amount| -- so it is built directly from that centre rather than inferred
            // from tangents (which degenerates as a corner flattens toward straight).
            // A converging corner is forced to take the long way round (a loop that
            // exits the shape and returns); a diverging corner takes the short way (a
            // round join). The untrimmed offset edges then cross each other at the
            // mitre points -- the crossings the extraction step will cut at.
            std::vector<std::unique_ptr<Stoicheion>> joinArc(corners);
            for (size_t k = 0; k < corners; k++) {
                Pos corner = eEnd(*edges[src[k]]);                    // the original corner
                Pos endA = eEnd(*off[k]);
                Pos startB = eStart(*off[(k + 1) % n]);
                if ((endA - startB).pythag() < 1e-4f) { continue; }   // tangent-continuous: already joined

                // fuseCloseTangencies: a seam whose corner angle is nearly zero
                // (practically tangent spans, the signature of STEP-derived
                // sections) gets NO join loop -- the offset endpoints are fused
                // to their average. Where an endpoint cannot move (an offset
                // ellipse), a straight micro-segment seals the seam instead.
                if (fuseCloseTangencies) {
                    Pos ta = eEndDir(*off[k]);
                    Pos tb = eStartDir(*off[(k + 1) % n]);
                    if (std::fabs(ta.cross(tb)) < FuseSin && ta.dot(tb) > 0.0f) {
                        Pos mid = (endA + startB) * 0.5f;
                        bool aOk = setEdgeEnd(*off[k], mid);
                        bool bOk = setEdgeStart(*off[(k + 1) % n], mid);
                        if (!aOk || !bOk) {
                            joinArc[k] = std::make_unique<Segment2>(
                                eEnd(*off[k]), eStart(*off[(k + 1) % n]));
                        }
                        continue;
                    }
                }

                // All angle math in DOUBLE: a nearly-complete around-the-bend arc
                // subtracts two nearly-equal angles, which float cannot survive.
                dPos C(corner);
                dPos radial = dPos(endA) - C;
                double r = radial.len();
                if (r < 1e-12) { continue; }
                // Travel sense around the corner: does leaving along A's direction head
                // CCW about the centre? Place the through-point d at the swept midpoint.
                int s = (dPos(eEndDir(*off[k])).dot(radial.perp()) >= 0.0) ? 1 : -1;
                double angA = radial.angle(), angB = (dPos(startB) - C).angle();
                double span = (s > 0) ? dWrap(angB - angA) : dWrap(angA - angB);
                double midAng = angA + static_cast<double>(s) * span * 0.5;
                joinArc[k] = std::make_unique<Arc2>(corner, endA, startB,
                                                    (C + dPos::fromAngle(midAng) * r).f());
            }

            for (size_t i = 0; i < n; i++) {
                result.edges.push_back(std::move(off[i]));
                if (i < corners && joinArc[i]) { result.edges.push_back(std::move(joinArc[i])); }
            }
            return result;
        }

        // Intersections -- exact crossings of the (possibly self-intersecting) offset.
        //--------------------------------------------------

        // True (with the parameter) when a point lies strictly inside a segment
        // span. Strict interiority is measured in absolute DISTANCE from the
        // endpoints, never in parameter space -- a parameter tolerance scales with
        // edge length, carving a fat exclusion zone at the ends of long edges
        // (which is exactly where a collapsing shape's self-crossings migrate).
        static bool onSeg(Pos a, Pos b, Pos p, float& t) {
            dPos A(a), D = dPos(b) - dPos(a), P(p);
            double l2 = D.dot(D);
            if (l2 < 1e-18) { return false; }
            double td = (P - A).dot(D) / l2;
            t = static_cast<float>(td);
            if (td < 0.0 || td > 1.0) { return false; }
            return (P - A).len() > 1e-3 && (P - dPos(b)).len() > 1e-3;
        }
        // True when a point on a circular edge's circle lies strictly inside its
        // swept range. A full circle (sweep == TAU) contains its whole circumference
        // -- it has no endpoints to exclude. Interiority is measured as absolute ARC
        // LENGTH from the ends (angle * radius), never as a bare angle, which would
        // scale the exclusion zone with the radius.
        static bool onSpan(Pos c, float a0, float sweep, Pos p) {
            if (sweep >= TAU - 1e-6f) { return true; }
            dPos d = dPos(p) - dPos(c);
            double rel = dWrap(d.angle() - static_cast<double>(a0));
            if (rel > static_cast<double>(sweep)) { return false; }
            double r = d.len();
            return rel * r > 1e-3 && (static_cast<double>(sweep) - rel) * r > 1e-3;
        }

        // Strict-interior test for an offset-ellipse hit (a full loop has no
        // endpoints to exclude).
        static bool oeInterior(const OffsetEllipse2& oe, Pos q) {
            if (oe.fullLoop()) { return true; }
            return (q - oe.pointAt(0.0f)).pythag() > 1e-3f && (q - oe.pointAt(1.0f)).pythag() > 1e-3f;
        }

        // Crossings involving an offset ellipse, by certified scan-and-bisect on
        // its span. Against a line the root function is the signed side; against
        // a circle, the radial excess; against another offset ellipse, the second
        // curve is chordised finely and each chord handled as a line (chord
        // sagitta is far below the method's working tolerance).
        static void offEllCross(const Stoicheion& A, const Stoicheion& B, std::vector<Pos>& out) {
            if (!isOffEll(A)) { offEllCross(B, A, out); return; }
            const OffsetEllipse2& oe = *asOff(A);
            auto P = [&](double s) { return dPos(oe.pointAt(static_cast<float>(s))); };

            auto push = [&](Pos q) {
                for (const Pos& o : out) { if ((o - q).pythag() < 1e-4f) { return; } }   // dedupe
                if (!transversalAt(A, B, q)) { return; }                                 // kisses don't count
                out.push_back(q);
            };
            auto acceptChord = [&](dPos a, dPos dvec, double len2, const std::vector<double>& roots,
                                   const std::function<bool(Pos)>& otherInterior) {
                for (double s : roots) {
                    dPos q = P(s);
                    double u = (q - a).dot(dvec) / len2;
                    if (u < 0.0 || u > 1.0) { continue; }
                    Pos qp = q.f();
                    if (!oeInterior(oe, qp) || !otherInterior(qp)) { continue; }
                    push(qp);
                }
            };

            if (isOffEll(B)) {
                const OffsetEllipse2& ob = *asOff(B);
                const int NB = 96;
                dPos prev(ob.pointAt(0.0f));
                for (int i = 1; i <= NB; i++) {
                    dPos cur(ob.pointAt(static_cast<float>(i) / NB));
                    dPos dvec = cur - prev; double len2 = dvec.dot(dvec);
                    if (len2 > 1e-18) {
                        dPos a = prev;
                        std::vector<double> roots;
                        scanRoots([&](double s) { return dvec.cross(P(s) - a); }, 128, roots);
                        acceptChord(a, dvec, len2, roots, [&](Pos q) { return oeInterior(ob, q); });
                    }
                    prev = cur;
                }
                return;
            }

            Pos cb; float rb, a0b, swb; int chb;
            if (circularOf(B, cb, rb, a0b, swb, chb)) {
                std::vector<double> roots;
                scanRoots([&](double s) { return (P(s) - dPos(cb)).len() - static_cast<double>(rb); }, 256, roots);
                for (double s : roots) {
                    Pos q = P(s).f();
                    if (oeInterior(oe, q) && onSpan(cb, a0b, swb, q)) { push(q); }
                }
                return;
            }

            // segment
            dPos a(eStart(B)), b(eEnd(B));
            dPos dvec = b - a; double len2 = dvec.dot(dvec);
            if (len2 < 1e-18) { return; }
            std::vector<double> roots;
            scanRoots([&](double s) { return dvec.cross(P(s) - a); }, 256, roots);
            acceptChord(a, dvec, len2, roots, [&](Pos q) {
                float t;
                return onSeg(eStart(B), eEnd(B), q, t);
            });
        }

        // A reported intersection only counts if the two curves actually EXCHANGE
        // SIDES there. A tangential kiss does not: the crossing walk's delta is
        // sign(t x t'), which at a tangency is pure float noise -- a coin flip
        // injected into the accumulated numbers -- and the cut it forces creates
        // pieces whose classification is inherently ambiguous. A kiss is not a
        // crossing by the definition of what the walk counts, so it is rejected at
        // detection, not adjudicated downstream.
        static bool transversalAt(const Stoicheion& A, const Stoicheion& B, Pos q) {
            Pos ta = travelDirAt(A, q), tb = travelDirAt(B, q);
            return std::fabs(ta.cross(tb)) > 1e-3f;        // unit tangents: sin of the angle
        }

        // Genuine crossing points of two edges (segments, arcs, full circles or
        // offset ellipses), strictly inside both spans -- transversal only.
        static void edgeCross(const Stoicheion& A, const Stoicheion& B, std::vector<Pos>& out) {
            if (isOffEll(A) || isOffEll(B)) { offEllCross(A, B, out); return; }
            Pos ca, cb; float ra, rb, a0a, a0b, swa, swb; int cha, chb;
            bool aa = circularOf(A, ca, ra, a0a, swa, cha);
            bool bb = circularOf(B, cb, rb, a0b, swb, chb);
            if (!aa && !bb) {
                Pos p; if (!lineLineInf(eStart(A), eEnd(A), eStart(B), eEnd(B), p)) { return; }
                float t, u;
                if (onSeg(eStart(A), eEnd(A), p, t) && onSeg(eStart(B), eEnd(B), p, u)
                    && transversalAt(A, B, p)) { out.push_back(p); }
            }
            else if (!aa && bb) {
                std::vector<Pos> cand; lineCircleInf(eStart(A), eEnd(A), cb, rb, cand);
                for (Pos p : cand) {
                    float t;
                    if (onSeg(eStart(A), eEnd(A), p, t) && onSpan(cb, a0b, swb, p)
                        && transversalAt(A, B, p)) { out.push_back(p); }
                }
            }
            else if (aa && !bb) {
                std::vector<Pos> cand; lineCircleInf(eStart(B), eEnd(B), ca, ra, cand);
                for (Pos p : cand) {
                    float t;
                    if (onSeg(eStart(B), eEnd(B), p, t) && onSpan(ca, a0a, swa, p)
                        && transversalAt(A, B, p)) { out.push_back(p); }
                }
            }
            else {
                std::vector<Pos> cand; circleCircle(ca, ra, cb, rb, cand);
                for (Pos p : cand) {
                    if (onSpan(ca, a0a, swa, p) && onSpan(cb, a0b, swb, p)
                        && transversalAt(A, B, p)) { out.push_back(p); }
                }
            }
        }

        // *Every* genuine crossing point of this chain with itself (strictly inside
        // both edges' spans, so deliberate vertex junctions never count).
        std::vector<Pos> allSelfIntersections() const {
            std::vector<Pos> out;
            size_t n = edges.size();
            for (size_t i = 0; i < n; i++) {
                for (size_t j = i + 1; j < n; j++) {
                    std::vector<Pos> pts; edgeCross(*edges[i], *edges[j], pts);
                    for (const Pos& p : pts) { out.push_back(p); }
                }
            }
            return out;
        }

        // Chirality: +1 CCW, -1 CW, 0 degenerate. An ABSOLUTE property of the chain,
        // read directly off the geometry: the exact signed area is half the sum of
        // each edge's closed-form ∮(x dy - y dx) term (Green's theorem) -- no
        // tessellation, no sampling, no comparison to anything remembered.
        int windingSign() const {
            float twiceArea = 0.0f;
            for (const auto& e : edges) { twiceArea += e->signedAreaTerm(); }
            return (twiceArea > 1e-4f) ? 1 : (twiceArea < -1e-4f ? -1 : 0);
        }

        // Extraction -- splitting the blind offset at its crossings.
        //--------------------------------------------------
        // Splitting NEVER alters direction: a sub-edge travels exactly the way its
        // parent did, and an arc's halves keep the parent's chirality. The split is
        // pure subdivision -- both halves laid end to end retrace the parent exactly.

        // Parameter (0..1) of a point along an edge's travel (for ordering cuts).
        // A full circle parameterises by travel angle from its point a.
        static float paramOnEdge(const Stoicheion& e, Pos p) {
            if (isArc(e)) {
                const Arc2* a = asArc(e);
                float a0, sweep; a->range(a0, sweep);
                if (sweep < 1e-9f) { return 0.0f; }
                double rel = dWrap((dPos(p) - dPos(a->c)).angle() - static_cast<double>(a0))
                           / static_cast<double>(sweep);
                return static_cast<float>((a->chirality() > 0) ? rel : 1.0 - rel);   // along TRAVEL, not CCW
            }
            if (isCircle(e)) {
                const Circle2* k = asCirc(e);
                dPos C(k->c);
                double rel = dWrap((dPos(p) - C).angle() - (dPos(k->a) - C).angle()) / dTAU;
                return static_cast<float>((k->chirality() > 0) ? rel : ((rel > 1e-12) ? 1.0 - rel : 0.0));
            }
            if (isOffEll(e)) { return oeParamOf(*asOff(e), p); }   // already a travel coordinate
            dPos s(eStart(e)), d = dPos(eEnd(e)) - dPos(eStart(e));
            double l2 = d.dot(d);
            return (l2 > 1e-18) ? static_cast<float>((dPos(p) - s).dot(d) / l2) : 0.0f;
        }

        // Split an edge at a point on it, preserving kind, direction and chirality:
        // a line into two lines travelling the same way; an arc into two concentric
        // arcs, each travelling the parent's way round (through-points re-seated on
        // the parent's side, so chirality is inherited, never recomputed).
        static void splitEdge(const Stoicheion& e, Pos p,
                              std::unique_ptr<Stoicheion>& first, std::unique_ptr<Stoicheion>& second) {
            if (isArc(e)) {
                const Arc2* arc = asArc(e);
                dPos C(arc->c);
                double r = (dPos(arc->a) - C).len();
                dPos s = C + (dPos(p) - C).unit() * r;                   // exact point on the circle
                int sgn = arc->chirality();
                double angA = (dPos(arc->a) - C).angle();
                double angS = (s - C).angle();
                double angB = (dPos(arc->b) - C).angle();
                double midL = (sgn > 0) ? (angA + dWrap(angS - angA) * 0.5) : (angA - dWrap(angA - angS) * 0.5);
                double midR = (sgn > 0) ? (angS + dWrap(angB - angS) * 0.5) : (angS - dWrap(angS - angB) * 0.5);
                first  = std::make_unique<Arc2>(arc->c, Pos(arc->a), s.f(), (C + dPos::fromAngle(midL) * r).f());
                second = std::make_unique<Arc2>(arc->c, s.f(), Pos(arc->b), (C + dPos::fromAngle(midR) * r).f());
            }
            else if (isOffEll(e)) {
                // Pure interval arithmetic: cut the parameter span at the point's
                // travel coordinate. Direction and geometry are untouched.
                const OffsetEllipse2* o = asOff(e);
                float tm = o->paramAt(oeParamOf(*o, p));
                first  = std::make_unique<OffsetEllipse2>(o->c, o->u, o->v, o->d, o->a0, tm);
                second = std::make_unique<OffsetEllipse2>(o->c, o->u, o->v, o->d, tm, o->a1);
            }
            else {
                const Segment2* seg = asSeg(e);
                first  = std::make_unique<Segment2>(Pos(seg->a), p);
                second = std::make_unique<Segment2>(p, Pos(seg->b));
            }
        }

        // Split an edge at every cut point on it, sorted along its travel, into
        // sub-edges emitted in travel order. A full circle has no endpoints, so its
        // splitting is CYCLIC: n cuts make a ring of n arcs, wrapping around -- each
        // arc travelling the circle's own way (chirality inherited, never inferred).
        static void splitEdgeAtPoints(const Stoicheion& e, const std::vector<Pos>& points,
                                      std::vector<std::unique_ptr<Stoicheion>>& out) {
            if (isCircle(e) && points.size() >= 2) {
                const Circle2* k = asCirc(e);
                float r = k->radius();
                int s = k->chirality();
                std::vector<std::pair<float, Pos>> sp;
                for (const Pos& p : points) {
                    Pos q = k->c + (p - k->c).normalized() * r;          // exact point on the circle
                    sp.push_back({ paramOnEdge(e, q), q });
                }
                std::sort(sp.begin(), sp.end(),
                          [](const std::pair<float, Pos>& a, const std::pair<float, Pos>& b) { return a.first < b.first; });
                dPos C(k->c);
                for (size_t i = 0; i < sp.size(); i++) {
                    const Pos& pa = sp[i].second;
                    const Pos& pb = sp[(i + 1) % sp.size()].second;
                    float dt = sp[(i + 1) % sp.size()].first - sp[i].first;
                    if (i + 1 == sp.size()) { dt += 1.0f; }              // the wrap span
                    if (dt * TAU * r < 1e-3f) { continue; }              // coincident cuts (absolute arc length)
                    double angA = (dPos(pa) - C).angle(), angB = (dPos(pb) - C).angle();
                    double span = (s > 0) ? dWrap(angB - angA) : dWrap(angA - angB);
                    double midAng = angA + static_cast<double>(s) * span * 0.5;
                    out.push_back(std::make_unique<Arc2>(k->c, pa, pb,
                                                         (C + dPos::fromAngle(midAng) * static_cast<double>(r)).f()));
                }
                return;
            }
            if (isCircle(e)) { out.push_back(e.clone()); return; }       // 0..1 cuts: unsplittable
            if (isOffEll(e) && asOff(e)->fullLoop()) {
                // A full-span parallel loop also splits CYCLICALLY (no endpoints):
                // n cuts make a ring of n parameter intervals, wrapping around.
                if (points.size() < 2) { out.push_back(e.clone()); return; }
                const OffsetEllipse2* o = asOff(e);
                std::vector<float> ss;
                for (const Pos& p : points) { ss.push_back(oeParamOf(*o, p)); }
                std::sort(ss.begin(), ss.end());
                float total = edgeLength(e);
                for (size_t i = 0; i < ss.size(); i++) {
                    float sa = ss[i];
                    float sb = ss[(i + 1) % ss.size()];
                    float ds = (i + 1 == ss.size()) ? (sb + 1.0f - sa) : (sb - sa);
                    if (ds * total < 1e-3f) { continue; }                // coincident cuts
                    float ta = o->paramAt(sa);
                    float tb = o->paramAt(sa + ds);                      // may pass the wrap
                    out.push_back(std::make_unique<OffsetEllipse2>(o->c, o->u, o->v, o->d, ta, tb));
                }
                return;
            }
            if (points.empty()) { out.push_back(e.clone()); return; }
            std::vector<std::pair<float, Pos>> sp;
            for (const Pos& p : points) { sp.push_back({ paramOnEdge(e, p), p }); }
            std::sort(sp.begin(), sp.end(),
                      [](const std::pair<float, Pos>& a, const std::pair<float, Pos>& b) { return a.first < b.first; });
            std::unique_ptr<Stoicheion> rem = e.clone();
            Pos lastCut = eStart(e);
            for (const auto& pr : sp) {
                // Endpoint / duplicate filters in absolute DISTANCE (a parameter
                // tolerance would scale with edge length).
                if ((pr.second - lastCut).pythag() <= 1e-3f) { continue; }
                if ((pr.second - eEnd(e)).pythag() <= 1e-3f) { continue; }
                std::unique_ptr<Stoicheion> first, second;
                splitEdge(*rem, pr.second, first, second);
                out.push_back(std::move(first));
                rem = std::move(second);
                lastCut = pr.second;
            }
            out.push_back(std::move(rem));
        }

        // The exact signed area enclosed by this chain: half the Green's-theorem
        // sum of each edge's closed-form ∮(x dy - y dx) term. Positive = CCW.
        float signedArea() const {
            float twice = 0.0f;
            for (const auto& e : edges) { twice += e->signedAreaTerm(); }
            return 0.5f * twice;
        }

        // Arc length of an edge and the point at its travel midpoint.
        static float edgeLength(const Stoicheion& e) {
            Pos c; float r, a0, sweep; int chir;
            if (circularOf(e, c, r, a0, sweep, chir)) { return r * sweep; }
            if (isOffEll(e)) {
                const OffsetEllipse2* o = asOff(e);
                const int N = 32; float len = 0.0f;
                Pos prev = o->pointAt(0.0f);
                for (int i = 1; i <= N; i++) {
                    Pos p = o->pointAt(static_cast<float>(i) / N);
                    len += (p - prev).pythag(); prev = p;
                }
                return len;
            }
            return (eEnd(e) - eStart(e)).pythag();
        }
        static Pos edgeMidpoint(const Stoicheion& e) {
            Pos c; float r, a0, sweep; int chir;
            if (circularOf(e, c, r, a0, sweep, chir)) { return c + Pos::fromAngle(a0 + sweep * 0.5f) * r; }
            if (isOffEll(e)) { return asOff(e)->pointAt(0.5f); }
            return (eStart(e) + eEnd(e)) * 0.5f;
        }
        // A point a fraction f in [0,1] along an edge's body, IN TRAVEL ORDER:
        // f=0 is the travel start, f=1 the travel end. circularOf reports the
        // CCW span regardless of travel, so a CW edge walks its span backwards
        // -- from the span's far end down to a0.
        static Pos edgePointAt(const Stoicheion& e, float f) {
            Pos c; float r, a0, sweep; int chir;
            if (circularOf(e, c, r, a0, sweep, chir)) {
                float start = (chir > 0) ? a0 : a0 + sweep;
                return c + Pos::fromAngle(start + static_cast<float>(chir) * sweep * f) * r;
            }
            if (isOffEll(e)) { return asOff(e)->pointAt(f); }
            return eStart(e) + (eEnd(e) - eStart(e)) * f;
        }

        // The point of greatest x anywhere on an edge / on a whole loop. The loop
        // attaining a cluster's global maximum x is PROVABLY unenclosed (nothing
        // reaches beyond the rightmost point) -- an exact identification of an
        // outermost loop, with no winding probe and no sample heuristics.
        static Pos edgeMaxXPoint(const Stoicheion& e) {
            Pos c; float r, a0, sweep; int chir;
            if (circularOf(e, c, r, a0, sweep, chir)) {
                Pos best = (eStart(e).x >= eEnd(e).x) ? eStart(e) : eEnd(e);
                if (sweep >= TAU - 1e-6f || wrapTau(-a0) <= sweep) {
                    Pos east = c + Pos(r, 0.0f);
                    if (east.x > best.x) { best = east; }
                }
                return best;
            }
            if (isOffEll(e)) {
                const OffsetEllipse2* o = asOff(e);
                Pos best = o->pointAt(0.0f);
                const int N = 64;
                for (int i = 1; i <= N; i++) {
                    Pos p = o->pointAt(static_cast<float>(i) / N);
                    if (p.x > best.x) { best = p; }
                }
                return best;
            }
            return (eStart(e).x >= eEnd(e).x) ? eStart(e) : eEnd(e);
        }
        static Pos loopMaxXPoint(const Chain& L) {
            Pos best(-1e30f, 0.0f);
            for (const auto& e : L.edges) {
                Pos p = edgeMaxXPoint(*e);
                if (p.x > best.x) { best = p; }
            }
            return best;
        }

        // The winding number of this chain around a probe point, by ACCUMULATED
        // SUBTENDED ANGLE -- deliberately NOT ray casting. A ray extends through
        // the whole plane, so its count flips when DISTANT geometry slides across
        // the ray line (a razor-edge double crossing at a far-away vertex: action
        // at a distance, the classic source of judgement flicker that correlates
        // with nothing local). The angle sum is continuous in every vertex
        // position -- it can only destabilise when the probe sits ON the curve.
        // Segments contribute their exact wrapped bearing delta; circular spans
        // contribute exactly via the inside/outside case (from inside the circle,
        // the bearing advances monotonically, fixing the 2-pi branch uniquely);
        // offset ellipses contribute by fine chordal sampling.
        int windingAround(Pos probe) const {
            const dPos P(probe);
            double total = 0.0;

            auto chord = [&](dPos a, dPos b) {
                dPos u = a - P, v = b - P;
                return std::atan2(u.cross(v), u.dot(v));   // wrapped to (-pi, pi]
            };

            for (const auto& e : edges) {
                Pos c; float r, a0, sweep; int chir;
                if (circularOf(*e, c, r, a0, sweep, chir)) {
                    dPos C(c);
                    double R = r;
                    dPos p0 = C + dPos::fromAngle(a0) * R;
                    dPos p1 = C + dPos::fromAngle(static_cast<double>(a0) + sweep) * R;
                    double wrapped = chord(p0, p1);          // CCW-span bearing delta, mod 2pi
                    double d;
                    if ((P - C).len() < R) {
                        // Inside the circle: the bearing advances monotonically CCW
                        // with the parameter, so the true advance lies in (0, 2pi)
                        // and the branch is unique.
                        d = (wrapped > 0.0) ? wrapped : wrapped + dTAU;
                    }
                    else {
                        d = wrapped;                         // outside: |advance| < pi
                    }
                    total += (chir > 0) ? d : -d;            // travel direction signs it
                }
                else if (isOffEll(*e)) {
                    const OffsetEllipse2* o = asOff(*e);
                    const int N = 256;
                    dPos prev(o->pointAt(0.0f));
                    for (int i = 1; i <= N; i++) {
                        dPos cur(o->pointAt(static_cast<float>(i) / N));
                        total += chord(prev, cur);
                        prev = cur;
                    }
                }
                else {
                    total += chord(dPos(eStart(*e)), dPos(eEnd(*e)));
                }
            }
            return static_cast<int>(std::lround(total / dTAU));
        }

        // The path's total accumulated turning, in radians: each edge's own swept
        // turning (an arc turns by chirality * sweep; a segment doesn't turn) plus
        // the signed exterior angle at every junction. For a closed path this is
        // 2*pi times the rotation index -- the path's handedness as TURNING. Robust
        // where signed area is not: burr lobes can outweigh a thin valid loop's
        // area and flip its sign, but they can never flip the total turning, since
        // offsetting preserves the tangent structure of its source.
        float totalTurning() const {
            const size_t n = edges.size();
            if (n == 0) { return 0.0f; }
            double total = 0.0;
            for (const auto& e : edges) {
                Pos c; float r, a0, sweep; int chir;
                if (circularOf(*e, c, r, a0, sweep, chir)) { total += static_cast<double>(chir) * sweep; }
                else if (isOffEll(*e)) {
                    // The edge's own bending, accumulated from sampled tangents
                    // (each wrapped delta is well under pi at this sampling).
                    const OffsetEllipse2* o = asOff(*e);
                    const int N = 64;
                    dPos prev(o->tangentAt(0.0f));
                    for (int i = 1; i <= N; i++) {
                        dPos t(o->tangentAt(static_cast<float>(i) / N));
                        total += std::atan2(prev.cross(t), prev.dot(t));
                        prev = t;
                    }
                }
            }
            const size_t corners = closed ? n : (n - 1);
            for (size_t k = 0; k < corners; k++) {
                dPos a(eEndDir(*edges[k]));
                dPos b(eStartDir(*edges[(k + 1) % n]));
                total += std::atan2(a.cross(b), a.dot(b));   // signed exterior angle
            }
            return static_cast<float>(total);
        }
        int turningSign() const {
            float t = totalTurning();
            return (t > 1e-3f) ? 1 : (t < -1e-3f) ? -1 : 0;
        }

        // Unit travel tangent of an edge at a point on it (the direction the path
        // moves through `p` when traversing this edge).
        static Pos travelDirAt(const Stoicheion& e, Pos p) {
            Pos c; float r, a0, sweep; int chir;
            if (circularOf(e, c, r, a0, sweep, chir)) {
                Pos t = perpCCW(p - c);
                if (chir < 0) { t = t * -1.0f; }
                return t.normalized();
            }
            if (isOffEll(e)) { const OffsetEllipse2* o = asOff(e); return o->tangentAt(oeParamOf(*o, p)); }
            return eStartDir(e);
        }

        // Extraction strategy TWO: the accumulated crossing number. The chain is
        // split at every self-crossing exactly as in the fracture -- but the
        // sub-edges are NOT regrouped into loops: they stay ONE chain, in the exact
        // order of the edges they were cut from, each travelling its parent's way.
        // Walking that order, every time the walk passes through a crossing we ask
        // which way the OTHER strand of the path crosses us: from our positive
        // (left) side to our negative (right) side counts -1, from negative to
        // positive counts +1. Each sub-edge is tagged with the count in effect while
        // traversing it -- the walk is "settled" wherever the count reads zero.
        // `this` is untouched; the result is built from clones.
        struct CrossingRun {
            std::vector<std::unique_ptr<Stoicheion>> edges;   // original order, direction preserved
            std::vector<int> number;                          // accumulated count per sub-edge
        };

        CrossingRun crossingNumbers(float eps = 1e-3f) const {
            const size_t n = edges.size();

            // 1. all crossing points, remembering which two edges meet at each.
            struct Rec { Pos p; size_t i, j; };
            std::vector<Rec> recs;
            std::vector<std::vector<Pos>> pts(n);
            for (size_t i = 0; i < n; i++) {
                for (size_t j = i + 1; j < n; j++) {
                    std::vector<Pos> c; edgeCross(*edges[i], *edges[j], c);
                    for (const Pos& p : c) { recs.push_back({ p, i, j }); pts[i].push_back(p); pts[j].push_back(p); }
                }
            }

            // 2. split every edge at its cuts; the fine sequence keeps chain order,
            //    and each sub-edge remembers which original edge it came from.
            CrossingRun run;
            std::vector<size_t> parent;
            for (size_t i = 0; i < n; i++) {
                splitEdgeAtPoints(*edges[i], pts[i], run.edges);
                while (parent.size() < run.edges.size()) { parent.push_back(i); }
            }

            // 3. walk strictly in order, accumulating. A sub-edge wears the count in
            //    effect as it is entered; the count then updates at its end vertex if
            //    that vertex is a crossing (matched back to the record, taking the
            //    OTHER strand's travel direction there).
            run.number.resize(run.edges.size(), 0);
            int count = 0;
            for (size_t k = 0; k < run.edges.size(); k++) {
                run.number[k] = count;
                Pos v = eEnd(*run.edges[k]);
                for (const Rec& r : recs) {
                    if ((r.p - v).pythag() >= eps) { continue; }
                    size_t other = (parent[k] == r.i) ? r.j
                                 : (parent[k] == r.j) ? r.i : static_cast<size_t>(-1);
                    if (other == static_cast<size_t>(-1)) { continue; }
                    Pos tOur = eEndDir(*run.edges[k]);
                    Pos tOther = travelDirAt(*edges[other], r.p);
                    // Other heading to our left (positive side): it crossed negative ->
                    // positive: +1. Heading to our right: positive -> negative: -1.
                    count += (tOur.cross(tOther) > 0.0f) ? 1 : -1;
                }
            }

            // Normalise RELATIVE TO THE MAX: the walk's raw numbers carry an
            // arbitrary start-dependent constant, but the maximum over the whole
            // chain is a global property of the walk -- so shifting it to 0 makes
            // every number start-invariant with no external measurement at all.
            // The scale reads: 0 (the max) = blue, -1 = green, -2 = red.
            if (!run.number.empty()) {
                int maxN = run.number.front();
                for (int v : run.number) { maxN = std::max(maxN, v); }
                for (int& v : run.number) { v -= maxN; }
            }
            return run;
        }

        // Fragment the crossing run into sub-chains: consecutive, coincident
        // sub-edges sharing the same accumulated crossing number stay together, and
        // the chain breaks exactly where the number changes (i.e. at a crossing).
        // Nothing is recomputed -- the numbers are taken as known from the walk, the
        // sub-edges keep their travel order and direction, and each fragment is a
        // well-formed chain by construction (its pieces were already end-to-start).
        // On a closed chain the walk's start vertex is arbitrary, so the first and
        // last fragments can be two halves of the same stretch -- same number,
        // coincident across the wrap -- and are merged, in travel order.
        // (NumberedChain holds a Chain by value, so it is defined after the close
        // of Chain itself; the method body follows it, out of line.)
        struct NumberedChain;
        std::vector<NumberedChain> fragmentByCrossingNumber(float eps = 1e-3f) const;

        // THE crossing method, end to end: this (source) chain in, valid offset
        // chains out. Blind left-offset, crossing-number walk, minimum-level
        // extraction, stitching, and (optionally) the chirality trial of the
        // number -- with `trial` false, every minimum-level chain is returned
        // unjudged. Returns empty when the trial convicts (an inverted profile
        // has no valid offset). Defined out of line, after NumberedChain.
        std::vector<Chain> offsetValid(float amount, bool trial = true, float eps = 1e-3f) const;

        // Mitosis -- resolve mutual overlap among well-formed loops that all carry
        // their material on the LEFT of travel (a CCW outer shrinking inward, a CW
        // island growing outward). Defined out of line, after NumberedChain.
        static std::vector<Chain> mitose(std::vector<Chain>& loops, float eps = 1e-3f);

        // Start-point freedom: a closed loop is the same cycle wherever it is
        // entered, so a strategy may re-seat where it BEGINS without touching
        // travel or geometry (the edge holding the new start is split there;
        // direction-preserving, like every split in this system).
        //--------------------------------------------------

        size_t longestEdgeIndex() const {
            size_t best = 0; float bestLen = -1.0f;
            for (size_t i = 0; i < edges.size(); i++) {
                float len = edgeLength(*edges[i]);
                if (len > bestLen) { bestLen = len; best = i; }
            }
            return best;
        }

        // The nearest point of this chain to `p` (and which edge holds it).
        size_t nearestPoint(Pos p, Pos& out) const {
            size_t best = 0; float bestDist = 1e30f;
            out = edges.empty() ? p : eStart(*edges.front());
            for (size_t i = 0; i < edges.size(); i++) {
                Pos foot;
                if (!edges[i]->footOnCurve(p, foot)) { foot = edgeMidpoint(*edges[i]); }
                float d = (p - foot).pythag();
                if (d < bestDist) { bestDist = d; best = i; out = foot; }
            }
            return best;
        }

        // This closed loop, entered at point `p` on edge `k`. Open chains
        // return unchanged. A lone circle re-seats by BECOMING two half-arcs
        // entered at `p` -- the same cycle, same travel, chirality inherited.
        Chain startedAt(size_t k, Pos p, float eps = 1e-4f) const {
            if (!closed || edges.empty() || k >= edges.size()) { return clone(); }

            const size_t n = edges.size();
            Chain out;
            out.closed = true;
            out.id = id; out.parent = parent; out.link = link;

            if (isCircle(*edges[k])) {
                Pos c; float r, a0, sweep; int chir;
                if (!circularOf(*edges[k], c, r, a0, sweep, chir)) { return clone(); }
                dPos C(c);
                dPos u = (dPos(p) - C).unit();
                Pos a = (C + u * r).f();
                Pos b = (C - u * r).f();
                auto quarter = [&](double side) {
                    dPos q(-u.y * chir * side, u.x * chir * side);   // +-90 deg, travel side
                    return (C + q * r).f();
                };
                out.edges.push_back(std::make_unique<Arc2>(c, a, b, quarter(1.0)));
                out.edges.push_back(std::make_unique<Arc2>(c, b, a, quarter(-1.0)));
                return out;
            }

            const Stoicheion& e = *edges[k];
            if ((p - eStart(e)).pythag() <= eps) {            // already a vertex: rotate only
                for (size_t i = 0; i < n; i++) { out.edges.push_back(edges[(k + i) % n]->clone()); }
            }
            else if ((p - eEnd(e)).pythag() <= eps) {         // the next vertex: rotate there
                for (size_t i = 0; i < n; i++) { out.edges.push_back(edges[(k + 1 + i) % n]->clone()); }
            }
            else {                                            // mid-edge: split and wrap
                std::unique_ptr<Stoicheion> first, second;
                splitEdge(e, p, first, second);
                out.edges.push_back(std::move(second));
                for (size_t i = 1; i < n; i++) { out.edges.push_back(edges[(k + i) % n]->clone()); }
                out.edges.push_back(std::move(first));
            }
            return out;
        }

        // Rigidly TRANSLATE an edge by a vector -- no offsetting, no re-radiusing,
        // every point moves identically. (Toolpathy logic, not geometric logic.)
        static std::unique_ptr<Stoicheion> translatedEdge(const Stoicheion& e, Pos v) {
            std::unique_ptr<Stoicheion> copy = e.clone();
            if (isArc(*copy)) {
                Arc2* a = static_cast<Arc2*>(copy.get());
                a->c += v; a->a += v; a->b += v; a->d += v;
            }
            else if (isCircle(*copy)) {
                Circle2* k = static_cast<Circle2*>(copy.get());
                k->c += v; k->a += v; k->b += v;
            }
            else if (isOffEll(*copy)) {
                static_cast<OffsetEllipse2*>(copy.get())->c += v;   // base centre carries the curve
            }
            else {
                Segment2* s = static_cast<Segment2*>(copy.get());
                s->a += v; s->b += v;
            }
            return copy;
        }

        // Open-air pre-push: every edge marked openAir is rigidly TRANSLATED
        // `amount` to the right of its travel (with material on the left, right is
        // the free air), and the gaps this opens to its neighbours are bridged with
        // straight segments. Run on profile 0 BEFORE the recursion; nothing
        // downstream treats these edges differently -- the chain is simply moved,
        // then offset exactly as always. The slight push guarantees the tool's
        // sweep clears the original open boundary with margin, fully covering the
        // corners against the open region.
        Chain withOpenAirPushed(float amount, float eps = 1e-4f) const {
            Chain out;
            out.closed = closed;
            const size_t n = edges.size();
            if (n == 0) { return out; }

            bool any = false;
            for (const auto& e : edges) { if (e->openAir) { any = true; break; } }
            if (!any) { return clone(); }

            // Translate open-air edges OUTWARD -- away from the chain's own
            // enclosed interior, measured, not assumed: a closed chain's interior
            // lies to the LEFT of travel iff its turning sign is positive, so
            // outward is the left normal scaled by -turningSign. Wrong-signing
            // this pushes the edge a full radius INTO the material, slicing the
            // chain apart -- so the side must come from the chain, never from a
            // convention about which way "right" is.
            const float outwardSign = (closed && turningSign() < 0) ? 1.0f : -1.0f;
            std::vector<std::unique_ptr<Stoicheion>> moved;
            for (const auto& e : edges) {
                if (!e->openAir) { moved.push_back(e->clone()); continue; }
                Pos mid = edgeMidpoint(*e);
                Pos t = travelDirAt(*e, mid);
                Pos outward = perpCCW(t) * outwardSign;     // away from the interior
                moved.push_back(translatedEdge(*e, outward * amount));
            }

            // Re-walk, bridging every gap a displacement opened.
            for (size_t i = 0; i < n; i++) {
                out.edges.push_back(std::move(moved[i]));
                bool last = (i + 1 == n);
                if (last && !closed) { break; }
                const Stoicheion& cur = *out.edges.back();
                const Stoicheion& nxt = last ? *out.edges.front() : *moved[(i + 1) % n];
                Pos a = eEnd(cur), b = eStart(nxt);
                if ((a - b).pythag() > eps) {
                    out.edges.push_back(std::make_unique<Segment2>(a, b));
                }
            }
            return out;
        }

        // Building
        //--------------------------------------------------

        // Grow one chain out of the remaining edge pool, connecting end-to-start
        // until it closes or runs out of neighbours. `allowReverse` permits
        // flipping an edge to make a connection -- legitimate when chaining a
        // user's unordered sketch strokes, FORBIDDEN when reassembling oriented
        // frontier pieces (a silent reversal there fabricates wrong-handed loops).
        static Chain buildOne(std::vector<std::unique_ptr<Stoicheion>>& pool, float eps,
                              bool allowReverse = true) {
            Chain c;
            if (pool.empty()) { return c; }
            c.edges.push_back(std::move(pool.front()));
            pool.erase(pool.begin());
            Pos loopStart = eStart(*c.edges.front());

            while (!pool.empty()) {
                Pos cur = eEnd(*c.edges.back());
                int best = -1; bool rev = false; float bestD = eps;
                for (size_t i = 0; i < pool.size(); i++) {
                    float dS = (cur - eStart(*pool[i])).pythag();
                    if (dS < bestD) { best = static_cast<int>(i); rev = false; bestD = dS; }
                    if (allowReverse) {
                        float dE = (cur - eEnd(*pool[i])).pythag();
                        if (dE < bestD) { best = static_cast<int>(i); rev = true; bestD = dE; }
                    }
                }
                if (best < 0) { break; }
                std::unique_ptr<Stoicheion> next = rev ? reversedEdge(*pool[best]) : std::move(pool[best]);
                pool.erase(pool.begin() + best);
                c.edges.push_back(std::move(next));
                if ((eEnd(*c.edges.back()) - loopStart).pythag() <= eps) { c.closed = true; break; }
            }
            return c;
        }

        // Build all chains from a set of entities. Segments/arcs chain up; a full
        // circle or ellipse is its own closed loop (offset analytically, not via a
        // polygon); an open elliptical arc is tessellated so it can chain to its
        // neighbours; datums / construction / points are ignored.
        static std::vector<Chain> build(const std::vector<std::unique_ptr<Stoicheion>>& src, float eps = 1e-3f) {
            std::vector<Chain> out;
            std::vector<std::unique_ptr<Stoicheion>> pool;

            // An ellipse (or elliptical arc) enters the chain EXACTLY, as parallel
            // edges at distance zero (OffsetEllipse2 with d = 0): the family the
            // true offset lives in, closed under offsetting forever -- recursion
            // never approximates. The span is cut at QUADRANT boundaries (the
            // curvature extrema): offset cusps form there, so every future
            // swallowtail self-loop straddles a cut and is seen by the pairwise
            // crossing test.
            auto ellipseToEdges = [&pool](Pos c, Pos u, Pos v, float t0, float span) {
                int spans = std::max(2, static_cast<int>(std::ceil(std::fabs(span) / (TAU * 0.25f))));
                for (int i = 0; i < spans; i++) {
                    float ta = t0 + span * (static_cast<float>(i) / spans);
                    float tb = t0 + span * (static_cast<float>(i + 1) / spans);
                    pool.push_back(std::make_unique<OffsetEllipse2>(c, u, v, 0.0f, ta, tb));
                }
            };

            for (const auto& e : src) {
                if (!e || e->locked || e->construction || e->isPoint()) { continue; }
                switch (e->type()) {
                    case SKind::Circle: {                                  // prime closed curve: its own loop
                        Chain c; c.closed = true; c.edges.push_back(e->clone()); out.push_back(std::move(c));
                        break;
                    }
                    case SKind::Segment:
                    case SKind::Arc: { pool.push_back(e->clone()); break; }
                    case SKind::Ellipse: {
                        const Ellipse2* el = static_cast<const Ellipse2*>(e.get());
                        ellipseToEdges(el->c, el->u, el->v, 0.0f, TAU);
                        break;
                    }
                    case SKind::EllipseArc: {
                        const EllipseArc2* ea = static_cast<const EllipseArc2*>(e.get());
                        ellipseToEdges(ea->c, ea->u, ea->v, ea->a0, ea->span());
                        break;
                    }
                    default: break;
                }
            }

            while (!pool.empty()) {
                Chain c = buildOne(pool, eps);
                if (c.edges.empty()) { break; }
                out.push_back(std::move(c));
            }

            // Every freshly built chain is a root of its own ancestral tree.
            for (Chain& c : out) { c.id = newId(); c.parent = 0; }

            return out;
        }
    };

    // A crossing-method fragment: a well-formed sub-chain plus the accumulated
    // crossing number its pieces share. (Out of line because it holds a Chain by
    // value, which is incomplete inside Chain's own definition.)
    struct Chain::NumberedChain { Chain chain; int number = 0; };

    inline std::vector<Chain::NumberedChain> Chain::fragmentByCrossingNumber(float eps) const {
        CrossingRun run = crossingNumbers(eps);
        std::vector<NumberedChain> out;

        for (size_t k = 0; k < run.edges.size(); k++) {
            bool fresh = out.empty()
                || out.back().number != run.number[k]
                || (eEnd(*out.back().chain.edges.back()) - eStart(*run.edges[k])).pythag() > eps;
            if (fresh) { out.push_back(NumberedChain{ Chain{}, run.number[k] }); }
            out.back().chain.edges.push_back(std::move(run.edges[k]));
        }

        // Wrap merge: the tail fragment flows into the head fragment.
        if (closed && out.size() > 1) {
            NumberedChain& head = out.front();
            NumberedChain& tail = out.back();
            if (head.number == tail.number &&
                (eEnd(*tail.chain.edges.back()) - eStart(*head.chain.edges.front())).pythag() <= eps) {
                for (auto& e : head.chain.edges) { tail.chain.edges.push_back(std::move(e)); }
                head.chain.edges = std::move(tail.chain.edges);
                out.pop_back();
            }
        }

        // A fragment whose end returns to its start is itself a closed loop.
        for (NumberedChain& nc : out) {
            if (nc.chain.edges.empty()) { continue; }
            nc.chain.closed =
                (eEnd(*nc.chain.edges.back()) - eStart(*nc.chain.edges.front())).pythag() <= eps;
        }
        return out;
    }

    // The crossing method, end to end (see the in-class declaration).
    inline std::vector<Chain> Chain::offsetValid(float amount, bool trial, float eps) const {
        std::vector<Chain> out;

        // CLOSED CHAINS ONLY: offsetting is a statement about a bounded region's
        // boundary, and an open chain bounds nothing -- there is no such thing as
        // offsetting one. (Open chains exist legitimately in RESULTS -- link
        // segments -- but never enter the offsetting machinery.)
        if (!closed) { return out; }

        // 1. blind offset, split at self-crossings, walk, fragment by level.
        Chain raw = offsetRaw(amount);
        std::vector<NumberedChain> frags = raw.fragmentByCrossingNumber(eps);
        if (frags.empty()) { return out; }

        // 2. extract and STITCH the minimum-level fragments: they arrive as open
        //    runs interrupted by the excursions, but meet end-to-start at the
        //    crossing vertices (at each crossing the min-level pool holds exactly
        //    one end and one start, so stitching is unambiguous, never reversing).
        int minLevel = frags.front().number;
        for (const NumberedChain& nc : frags) { minLevel = std::min(minLevel, nc.number); }
        std::vector<std::unique_ptr<Stoicheion>> pool;
        for (NumberedChain& nc : frags) {
            if (nc.number != minLevel) { continue; }
            for (auto& e : nc.chain.edges) { pool.push_back(std::move(e)); }
        }
        while (!pool.empty()) {
            Chain m = buildOne(pool, eps, false);   // oriented pieces: never reverse
            if (m.edges.empty()) { break; }
            m.id = newId();
            m.parent = id;                          // children of this source chain
            out.push_back(std::move(m));
        }

        // 3. the trial -- THE handedness rule: an outside can get a new inside,
        //    but an inside cannot get a new outside. A candidate's handedness may
        //    never EXCEED its source's: a clockwise source can legitimately give
        //    birth to a counterclockwise chain (an outset pinching off a new
        //    cavity), but a counterclockwise source producing a clockwise chain is
        //    inversion -- that candidate is discarded. (Judged only where both
        //    senses are measurable: closed source, closed candidate.)
        const int srcChir = turningSign();
        if (trial && closed && srcChir > 0) {
            std::erase_if(out, [](const Chain& m) {
                return m.closed && m.turningSign() < 0;
            });
        }
        return out;
    }

    // Mitosis. Every loop is split at its crossings with every OTHER loop, and a
    // piece survives iff it is a true FRONTIER of the material: exactly one of its
    // two sides lies inside the material region. No side is ever assumed -- the
    // material region is {points where (total winding of all loops) * s >= 1},
    // with the sign s read off the OUTERMOST loop's own turning (a loop wound by
    // no other loop), so clockwise and counterclockwise scenes measure their own
    // orientation. Both sides material = interior of the union (discarded); both
    // sides void = swallowed or escaped (discarded); one of each = frontier. The
    // survivors stitch back into loops at the crossing points (they meet there
    // exactly, so this is reassembly, not inference). A lone loop is returned
    // untouched: there is nothing to overlap.
    inline std::vector<Chain> Chain::mitose(std::vector<Chain>& allChains, float eps) {

        // Only CLOSED loops bound regions, so only they may be asked region
        // questions (winding, containment, frontier-ness). Open chains have no
        // inside -- they pass through untouched, at face value, and must never
        // poison a cluster with meaningless winding answers.
        std::vector<Chain> out;
        std::vector<Chain> loops;
        for (Chain& ch : allChains) {
            if (ch.closed && !ch.edges.empty()) { loops.push_back(std::move(ch)); }
            else if (!ch.edges.empty())         { out.push_back(std::move(ch)); }
        }

        const size_t n = loops.size();
        if (n == 1) { out.push_back(std::move(loops.front())); return out; }
        if (n == 0) { return out; }

        // Cluster loops by INTERACTION (they cross, or one contains the other).
        // Disjoint profiles are independent worlds: a loop must never be judged by
        // the handedness of a loop it doesn't touch.
        auto interacts = [&](size_t a, size_t b) {
            std::vector<Pos> x;
            for (const auto& ea : loops[a].edges) {
                for (const auto& eb : loops[b].edges) {
                    edgeCross(*ea, *eb, x);
                    if (!x.empty()) { return true; }
                }
            }
            // Containment, probed at each loop's own EXTREME point: guaranteed on
            // the loop and far from the other unless they genuinely touch -- so the
            // answer can only change at a real geometric event, never at a sample
            // threshold.
            if (loops[b].windingAround(loopMaxXPoint(loops[a])) != 0) { return true; }
            if (loops[a].windingAround(loopMaxXPoint(loops[b])) != 0) { return true; }
            return false;
        };
        std::vector<int> cluster(n, -1);
        int clusters = 0;
        for (size_t a = 0; a < n; a++) {
            if (cluster[a] >= 0) { continue; }
            cluster[a] = clusters;
            std::vector<size_t> queue{ a };                       // flood the component
            while (!queue.empty()) {
                size_t cur = queue.back(); queue.pop_back();
                for (size_t b = 0; b < n; b++) {
                    if (cluster[b] >= 0 || !interacts(cur, b)) { continue; }
                    cluster[b] = clusters;
                    queue.push_back(b);
                }
            }
            clusters++;
        }

        for (int c = 0; c < clusters; c++) {
            std::vector<size_t> mem;
            for (size_t a = 0; a < n; a++) { if (cluster[a] == c) { mem.push_back(a); } }

            // A lone loop has nothing to overlap: through untouched.
            if (mem.size() == 1) { out.push_back(std::move(loops[mem[0]])); continue; }

            // The cluster's material orientation. A CCW loop ASSERTS an inside
            // system -- unless it is a CAVITY: strictly contained in some CW loop
            // it never crosses (the pinched-off air of an outside system). Any
            // asserting CCW loop makes the cluster an inside system (s = +1);
            // with none, it is an outside system (s = -1). The two counterexamples
            // this rule reconciles: a CW trace's sealed CCW cavity must NOT flip
            // the system inside-out (the census's failure), and a CCW boundary
            // CROSSING a CW loop must not be out-voted by the CW loop merely
            // reaching further (the outermost rule's failure) -- when a CCW loop
            // has any standing to claim territory, the least-clockwise
            // interpretation wins. Everything here is exact: crossings and
            // containment, no samples, no size contests.
            int s = -1;
            for (size_t a : mem) {
                if (loops[a].turningSign() <= 0) { continue; }       // CCW loops only

                bool cavity = false;
                for (size_t b : mem) {
                    if (b == a || loops[b].turningSign() >= 0) { continue; }   // vs CW loops

                    bool crosses = false;
                    std::vector<Pos> x;
                    for (const auto& ea : loops[a].edges) {
                        for (const auto& eb : loops[b].edges) {
                            edgeCross(*ea, *eb, x);
                            if (!x.empty()) { crosses = true; break; }
                        }
                        if (crosses) { break; }
                    }
                    if (crosses) { continue; }                       // fighting, not sealed

                    if (loops[b].windingAround(loopMaxXPoint(loops[a])) != 0) {
                        cavity = true;                               // sealed inside this CW loop
                        break;
                    }
                }

                if (!cavity) { s = 1; break; }                       // an asserting CCW loop
            }

            std::vector<std::unique_ptr<Stoicheion>> kept;
            for (size_t a : mem) {
                for (const auto& edge : loops[a].edges) {

                    // Cut this edge wherever any other cluster loop crosses it.
                    std::vector<Pos> cuts;
                    for (size_t b : mem) {
                        if (b == a) { continue; }
                        for (const auto& eb : loops[b].edges) { edgeCross(*edge, *eb, cuts); }
                    }
                    std::vector<std::unique_ptr<Stoicheion>> pieces;
                    splitEdgeAtPoints(*edge, cuts, pieces);

                    // Frontier test: material (within this cluster) on exactly one side.
                    for (auto& piece : pieces) {
                        Pos mid = edgeMidpoint(*piece);
                        Pos t = travelDirAt(*piece, mid);

                        float clearance = 1e30f;
                        for (size_t b : mem) {
                            for (const auto& le : loops[b].edges) {
                                if (le.get() == edge.get()) { continue; }
                                clearance = std::min(clearance, le->distanceTo(mid));
                            }
                        }
                        float reach = std::max(1e-4f, std::min(1e-2f * edgeLength(*piece), 0.4f * clearance));

                        auto totalWinding = [&](Pos probe) {
                            int w = 0;
                            for (size_t b : mem) { w += loops[b].windingAround(probe); }
                            return w;
                        };
                        bool leftIn  = totalWinding(mid + perpCCW(t) * reach) * s >= 1;
                        bool rightIn = totalWinding(mid - perpCCW(t) * reach) * s >= 1;
                        if (leftIn != rightIn) {
                            piece->id = loops[a].id;   // provenance: which loop this piece came from
                            kept.push_back(std::move(piece));
                        }
                    }
                }
            }

            // Stitch the cluster's surviving pieces back into well-formed loops.
            // Oriented frontier pieces: reversal is forbidden -- if ends don't
            // meet forward, the piece set is wrong and must FAIL VISIBLY (an open
            // remnant) rather than be silently flipped into a wrong-handed loop.
            //
            // Ancestry: each offspring takes a fresh id; its parent is the parent
            // of its DOMINANT contributor (by length) -- contributors are
            // same-generation intermediates, so this lands on the previous
            // generation's chain, as the ancestral tree wants.
            while (!kept.empty()) {
                Chain stitched = buildOne(kept, eps, false);
                if (stitched.edges.empty()) { break; }

                size_t dominant = mem.front(); float bestLen = -1.0f;
                for (size_t a : mem) {
                    float len = 0.0f;
                    for (const auto& e : stitched.edges) {
                        if (e->id == loops[a].id) { len += edgeLength(*e); }
                    }
                    if (len > bestLen) { bestLen = len; dominant = a; }
                }
                stitched.id = newId();
                stitched.parent = loops[dominant].parent;

                out.push_back(std::move(stitched));
            }
        }
        return out;
    }

    // =====================================================================
    // Profile -- one GENERATION of a sketch's geometry: the set of chains that
    // together bound the material at one offset depth. Profile 0 is the chains
    // as drawn; profile n+1 is profile n offset by one step. The pair
    // offset() / offsetBy() mirrors Stoicheion's (in place / value-returning):
    // the entire method -- stage A per chain, stage B across chains -- hides
    // behind these two calls, so recursive offsetting is just a loop.
    // =====================================================================
    struct Profile {

        std::vector<Chain> chains;

        Profile() = default;
        Profile(Profile&&) = default;
        Profile& operator=(Profile&&) = default;

        Profile clone() const {
            Profile p;
            for (const Chain& c : chains) { p.chains.push_back(c.clone()); }
            return p;
        }

        bool empty() const { return chains.empty(); }

        // Profile 0: every chain the sketch's entities form.
        static Profile fromEntities(const std::vector<std::unique_ptr<Stoicheion>>& src) {
            Profile p;
            p.chains = Chain::build(src);
            return p;
        }

        // The complete method, one generation forward:
        //   stage A -- each chain offset in its own universe (Chain::offsetValid:
        //   blind offset, crossing walk, minimum level, stitching, handedness law);
        //   stage B -- the validated chains meet (Chain::mitose: cluster, frontier
        //   test, merge / carve / mitose).
        // An exhausted profile (everything inverted away) returns empty.
        // The net handedness of a profile: the sum of every closed chain's
        // turning sign (+1 CCW, -1 CW).
        int netChirality() const {
            int t = 0;
            for (const Chain& c : chains) { if (c.closed) { t += c.turningSign(); } }
            return t;
        }

        Profile offsetBy(float amount) const {
            Profile next;
            std::vector<Chain> valid;
            for (const Chain& c : chains) {
                if (!c.closed) { continue; }   // closed chains only: opens never offset
                for (Chain& m : c.offsetValid(amount)) { valid.push_back(std::move(m)); }
            }
            next.chains = Chain::mitose(valid);
            return next;
        }

        // In-place twin.
        void offset(float amount) { *this = offsetBy(amount); }
    };
}
