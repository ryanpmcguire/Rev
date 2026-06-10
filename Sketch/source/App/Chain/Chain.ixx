module;

#include <vector>
#include <memory>
#include <string>
#include <cmath>
#include <algorithm>
#include <utility>

export module Sketch.App.Chain;

import Rev.Core.Pos;

import Sketch.App.Geometry;

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
export namespace Sketch::App {

    using Rev::Core::Pos;

    struct Chain {

        std::vector<std::unique_ptr<Stoicheion>> edges;   // oriented: travel is start -> end
        bool closed = false;

        Chain() = default;
        Chain(Chain&&) = default;
        Chain& operator=(Chain&&) = default;

        Chain clone() const {
            Chain c; c.closed = closed;
            for (const auto& e : edges) { c.edges.push_back(e->clone()); }
            return c;
        }

        // Type probes / accessors. The chain holds segments, arcs and full circles
        // (ellipses are tessellated to segments at build time).
        static bool isArc(const Stoicheion& e)    { return std::string(e.kind()) == "arc"; }
        static bool isCircle(const Stoicheion& e) { return std::string(e.kind()) == "circle"; }
        static const Segment2* asSeg(const Stoicheion& e)  { return static_cast<const Segment2*>(&e); }
        static const Arc2*     asArc(const Stoicheion& e)  { return static_cast<const Arc2*>(&e); }
        static const Circle2*  asCirc(const Stoicheion& e) { return static_cast<const Circle2*>(&e); }
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
            return Pos(asSeg(e)->a);
        }
        static Pos eEnd(const Stoicheion& e) {
            if (isArc(e)) { return Pos(asArc(e)->b); }
            if (isCircle(e)) { return Pos(asCirc(e)->a); }
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
            const Segment2* s = asSeg(e);
            return (s->b - s->a).normalized();
        }

        // Reverse an edge's travel direction (so chain building can connect either end).
        static std::unique_ptr<Stoicheion> reversedEdge(const Stoicheion& e) {
            if (isArc(e)) { const Arc2* a = asArc(e); return std::make_unique<Arc2>(a->c, a->b, a->a, a->d); }
            const Segment2* s = asSeg(e);
            return std::make_unique<Segment2>(s->b, s->a);
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
                Pos na = a->c + (a->a - a->c) / r * nr;
                Pos nb = a->c + (a->b - a->c) / r * nr;
                float ld = (a->d - a->c).pythag();
                Pos nd = (ld > 1e-9f) ? (a->c + (a->d - a->c) / ld * nr) : Pos(a->d);
                return std::make_unique<Arc2>(a->c, na, nb, nd);
            }
            if (isCircle(e)) {
                const Circle2* k = asCirc(e);
                float r = k->radius();
                if (r < 1e-9f) { return e.clone(); }
                float nr = r - d * static_cast<float>(k->chirality());   // left = inward for CCW
                Pos na = k->c + (k->a - k->c) / r * nr;
                float lb = (k->b - k->c).pythag();
                Pos nb = (lb > 1e-9f) ? (k->c + (k->b - k->c) / lb * nr) : Pos(k->b);
                return std::make_unique<Circle2>(k->c, na, nb);
            }
            const Segment2* s = asSeg(e);
            Pos dir = s->b - s->a; float L = dir.pythag();
            if (L < 1e-9f) { return e.clone(); }
            Pos n = perpCCW(dir / L);                                   // left normal
            return std::make_unique<Segment2>(s->a + n * d, s->b + n * d);
        }

        // Infinite-support intersection primitives (extended line / full circle),
        // shared by the exact edge-crossing test below.
        static bool lineLineInf(Pos a0, Pos a1, Pos b0, Pos b1, Pos& out) {
            Pos r = a1 - a0, s = b1 - b0; float rxs = r.cross(s);
            if (std::fabs(rxs) < 1e-9f) { return false; }
            out = a0 + r * ((b0 - a0).cross(s) / rxs); return true;
        }
        static void lineCircleInf(Pos a0, Pos a1, Pos c, float rad, std::vector<Pos>& out) {
            Pos d = a1 - a0; float A = d.dot(d); if (A < 1e-12f) { return; }
            Pos f = a0 - c; float B = 2.0f * f.dot(d), C = f.dot(f) - rad * rad;
            float disc = B * B - 4.0f * A * C; if (disc < 0.0f) { return; }
            disc = std::sqrt(disc);
            out.push_back(a0 + d * ((-B - disc) / (2.0f * A)));
            if (disc > 1e-9f) { out.push_back(a0 + d * ((-B + disc) / (2.0f * A))); }
        }
        static void circleCircle(Pos c0, float r0, Pos c1, float r1, std::vector<Pos>& out) {
            Pos d = c1 - c0; float dist = d.pythag();
            if (dist < 1e-9f || dist > r0 + r1 + 1e-6f || dist < std::fabs(r0 - r1) - 1e-6f) { return; }
            float a = (r0 * r0 - r1 * r1 + dist * dist) / (2.0f * dist);
            float h2 = r0 * r0 - a * a; if (h2 < 0.0f) { h2 = 0.0f; }
            float h = std::sqrt(h2); Pos mid = c0 + d * (a / dist);
            Pos perp(-d.y / dist * h, d.x / dist * h);
            out.push_back(mid + perp); if (h > 1e-9f) { out.push_back(mid - perp); }
        }
        // The *raw* offset, done BLINDLY: every edge slides to the LEFT of its own
        // travel direction by `amount` -- no winding test, no preferred side. The
        // chain's chirality alone decides what that means: a CCW loop's interior is
        // on the left, so a positive amount insets it; a CW loop's interior is on the
        // right, so the very same operation offsets it outward. Inset vs outset is
        // emergent, never inferred. The result may self-intersect; those crossings
        // are the raw material for the chain extraction to come.
        //
        // Lines only for now (segments in, segments + join arcs out); arcs, circles
        // and ellipses rejoin once the line case is settled.
        Chain offsetRaw(float amount) const {
            Chain result;
            if (edges.empty()) { return result; }
            result.closed = closed;

            // A lone full circle is its own closed loop: no corners, just the blind
            // concentric left-offset (its own chirality decides inset vs outset).
            if (edges.size() == 1 && isCircle(*edges.front())) {
                result.edges.push_back(offsetLeft(*edges.front(), amount));
                return result;
            }

            // Guard: a chain holding anything we don't offset yet (an ellipse from
            // build()) passes through unchanged.
            for (const auto& e : edges) {
                std::string k = e->kind();
                if (k != "segment" && k != "arc") {
                    for (const auto& src : edges) { result.edges.push_back(src->clone()); }
                    return result;
                }
            }

            std::vector<std::unique_ptr<Stoicheion>> off;
            off.reserve(edges.size());
            for (const auto& e : edges) { off.push_back(offsetLeft(*e, amount)); }

            const size_t n = off.size();
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
                Pos corner = eEnd(*edges[k]);                         // the original corner
                Pos endA = eEnd(*off[k]);
                Pos startB = eStart(*off[(k + 1) % n]);
                if ((endA - startB).pythag() < 1e-4f) { continue; }   // tangent-continuous: already joined
                Pos radial = endA - corner;
                float r = radial.pythag();
                if (r < 1e-9f) { continue; }
                // Travel sense around the corner: does leaving along A's direction head
                // CCW about the centre? Place the through-point d at the swept midpoint.
                int s = (eEndDir(*off[k]).dot(perpCCW(radial)) >= 0.0f) ? 1 : -1;
                float angA = radial.angle(), angB = (startB - corner).angle();
                float span = (s > 0) ? wrapTau(angB - angA) : wrapTau(angA - angB);
                float midAng = angA + static_cast<float>(s) * span * 0.5f;
                joinArc[k] = std::make_unique<Arc2>(corner, endA, startB,
                                                    corner + Pos::fromAngle(midAng) * r);
            }

            for (size_t i = 0; i < n; i++) {
                result.edges.push_back(std::move(off[i]));
                if (i < corners && joinArc[i]) { result.edges.push_back(std::move(joinArc[i])); }
            }
            return result;
        }

        // Intersections -- exact crossings of the (possibly self-intersecting) offset.
        //--------------------------------------------------

        // True (with the parameter) when a point lies strictly inside a segment span.
        static bool onSeg(Pos a, Pos b, Pos p, float& t) {
            Pos d = b - a; float l2 = d.dot(d);
            if (l2 < 1e-12f) { return false; }
            t = (p - a).dot(d) / l2;
            return t > 1e-3f && t < 1.0f - 1e-3f;
        }
        // True when a point on a circular edge's circle lies strictly inside its
        // swept range. A full circle (sweep == TAU) contains its whole circumference
        // -- it has no endpoints to exclude.
        static bool onSpan(Pos c, float a0, float sweep, Pos p) {
            if (sweep >= TAU - 1e-6f) { return true; }
            float rel = wrapTau((p - c).angle() - a0);
            return rel > 1e-3f && rel < sweep - 1e-3f;
        }

        // Genuine crossing points of two edges (segments, arcs or full circles),
        // strictly inside both spans.
        static void edgeCross(const Stoicheion& A, const Stoicheion& B, std::vector<Pos>& out) {
            Pos ca, cb; float ra, rb, a0a, a0b, swa, swb; int cha, chb;
            bool aa = circularOf(A, ca, ra, a0a, swa, cha);
            bool bb = circularOf(B, cb, rb, a0b, swb, chb);
            if (!aa && !bb) {
                Pos p; if (!lineLineInf(eStart(A), eEnd(A), eStart(B), eEnd(B), p)) { return; }
                float t, u;
                if (onSeg(eStart(A), eEnd(A), p, t) && onSeg(eStart(B), eEnd(B), p, u)) { out.push_back(p); }
            }
            else if (!aa && bb) {
                std::vector<Pos> cand; lineCircleInf(eStart(A), eEnd(A), cb, rb, cand);
                for (Pos p : cand) { float t; if (onSeg(eStart(A), eEnd(A), p, t) && onSpan(cb, a0b, swb, p)) { out.push_back(p); } }
            }
            else if (aa && !bb) {
                std::vector<Pos> cand; lineCircleInf(eStart(B), eEnd(B), ca, ra, cand);
                for (Pos p : cand) { float t; if (onSeg(eStart(B), eEnd(B), p, t) && onSpan(ca, a0a, swa, p)) { out.push_back(p); } }
            }
            else {
                std::vector<Pos> cand; circleCircle(ca, ra, cb, rb, cand);
                for (Pos p : cand) { if (onSpan(ca, a0a, swa, p) && onSpan(cb, a0b, swb, p)) { out.push_back(p); } }
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
        static float paramOnEdge(const Stoicheion& e, Pos p) {
            if (isArc(e)) {
                const Arc2* a = asArc(e);
                float a0, sweep; a->range(a0, sweep);
                if (sweep < 1e-9f) { return 0.0f; }
                float rel = wrapTau((p - a->c).angle() - a0) / sweep;
                return (a->chirality() > 0) ? rel : 1.0f - rel;   // along TRAVEL, not CCW
            }
            Pos s = eStart(e), en = eEnd(e);
            Pos d = en - s; float l2 = d.dot(d);
            return (l2 > 1e-12f) ? (p - s).dot(d) / l2 : 0.0f;
        }

        // Split an edge at a point on it, preserving kind, direction and chirality:
        // a line into two lines travelling the same way; an arc into two concentric
        // arcs, each travelling the parent's way round (through-points re-seated on
        // the parent's side, so chirality is inherited, never recomputed).
        static void splitEdge(const Stoicheion& e, Pos p,
                              std::unique_ptr<Stoicheion>& first, std::unique_ptr<Stoicheion>& second) {
            if (isArc(e)) {
                const Arc2* arc = asArc(e);
                float r = arc->radius();
                Pos s = arc->c + (p - arc->c).normalized() * r;          // exact point on the circle
                int sgn = arc->chirality();
                float angA = (arc->a - arc->c).angle();
                float angS = (s - arc->c).angle();
                float angB = (arc->b - arc->c).angle();
                float midL = (sgn > 0) ? (angA + wrapTau(angS - angA) * 0.5f) : (angA - wrapTau(angA - angS) * 0.5f);
                float midR = (sgn > 0) ? (angS + wrapTau(angB - angS) * 0.5f) : (angS - wrapTau(angS - angB) * 0.5f);
                first  = std::make_unique<Arc2>(arc->c, Pos(arc->a), s, arc->c + Pos::fromAngle(midL) * r);
                second = std::make_unique<Arc2>(arc->c, s, Pos(arc->b), arc->c + Pos::fromAngle(midR) * r);
            }
            else {
                const Segment2* seg = asSeg(e);
                first  = std::make_unique<Segment2>(Pos(seg->a), p);
                second = std::make_unique<Segment2>(p, Pos(seg->b));
            }
        }

        // Split an edge at every cut point on it, sorted along its travel, into
        // sub-edges emitted in travel order.
        static void splitEdgeAtPoints(const Stoicheion& e, const std::vector<Pos>& points,
                                      std::vector<std::unique_ptr<Stoicheion>>& out) {
            if (points.empty()) { out.push_back(e.clone()); return; }
            std::vector<std::pair<float, Pos>> sp;
            for (const Pos& p : points) { sp.push_back({ paramOnEdge(e, p), p }); }
            std::sort(sp.begin(), sp.end(),
                      [](const std::pair<float, Pos>& a, const std::pair<float, Pos>& b) { return a.first < b.first; });
            std::unique_ptr<Stoicheion> rem = e.clone();
            float lastT = 0.0f;
            for (const auto& pr : sp) {
                if (pr.first <= lastT + 1e-3f || pr.first >= 1.0f - 1e-3f) { continue; }   // endpoint / duplicate
                std::unique_ptr<Stoicheion> first, second;
                splitEdge(*rem, pr.second, first, second);
                out.push_back(std::move(first));
                rem = std::move(second);
                lastT = pr.first;
            }
            out.push_back(std::move(rem));
        }

        // Extraction strategy ONE: the arrangement fracture. Find ALL self-crossings,
        // split EVERY edge at all of its cuts (each sub-edge keeping its parent's
        // direction and chirality), then walk the sub-edges in travel order, popping
        // a loop off whenever the walk returns to a vertex it already passed through.
        // `this` is untouched -- the result is built from clones.
        std::vector<Chain> extractLoopsByFracture(float eps = 1e-3f) const {
            size_t n = edges.size();

            // 1. all crossing points, gathered per edge.
            std::vector<std::vector<Pos>> pts(n);
            for (size_t i = 0; i < n; i++) {
                for (size_t j = i + 1; j < n; j++) {
                    std::vector<Pos> c; edgeCross(*edges[i], *edges[j], c);
                    for (const Pos& p : c) { pts[i].push_back(p); pts[j].push_back(p); }
                }
            }

            // 2. split every edge at its cuts -> the fine sequence, in chain order.
            std::vector<std::unique_ptr<Stoicheion>> fine;
            for (size_t i = 0; i < n; i++) { splitEdgeAtPoints(*edges[i], pts[i], fine); }

            // 3. trace simple loops, never reversing an edge.
            std::vector<Chain> loops;
            std::vector<std::unique_ptr<Stoicheion>> cur;
            for (auto& e : fine) {
                cur.push_back(std::move(e));
                Pos v = eEnd(*cur.back());
                int idx = -1;
                for (size_t k = 0; k + 1 < cur.size(); k++) {
                    if ((eStart(*cur[k]) - v).pythag() < eps) { idx = static_cast<int>(k); break; }
                }
                if (idx >= 0) {
                    Chain loop; loop.closed = true;
                    for (size_t k = static_cast<size_t>(idx); k < cur.size(); k++) { loop.edges.push_back(std::move(cur[k])); }
                    cur.erase(cur.begin() + idx, cur.end());
                    loops.push_back(std::move(loop));
                }
            }
            if (!cur.empty()) {
                Chain loop; loop.closed = true;
                for (auto& e : cur) { loop.edges.push_back(std::move(e)); }
                loops.push_back(std::move(loop));
            }
            return loops;
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

        // Building
        //--------------------------------------------------

        // Grow one chain out of the remaining edge pool, connecting end-to-start
        // (reversing edges as needed) until it closes or runs out of neighbours.
        static Chain buildOne(std::vector<std::unique_ptr<Stoicheion>>& pool, float eps) {
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
                    float dE = (cur - eEnd(*pool[i])).pythag();
                    if (dS < bestD) { best = static_cast<int>(i); rev = false; bestD = dS; }
                    if (dE < bestD) { best = static_cast<int>(i); rev = true;  bestD = dE; }
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

            for (const auto& e : src) {
                if (!e || e->locked || e->construction || e->isPoint()) { continue; }
                std::string k = e->kind();
                if (k == "circle" || k == "ellipse") {                     // prime closed curve: its own loop
                    Chain c; c.closed = true; c.edges.push_back(e->clone()); out.push_back(std::move(c));
                }
                else if (k == "segment" || k == "arc") { pool.push_back(e->clone()); }
                else {                                                    // elliptical arc (open) -> polyline
                    std::vector<Pos> pts; e->tessellate(pts);
                    for (size_t i = 0; i + 1 < pts.size(); i++) { pool.push_back(std::make_unique<Segment2>(pts[i], pts[i + 1])); }
                }
            }

            while (!pool.empty()) {
                Chain c = buildOne(pool, eps);
                if (c.edges.empty()) { break; }
                out.push_back(std::move(c));
            }
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
}
