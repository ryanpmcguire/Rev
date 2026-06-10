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
// the unit of 2D toolpath offsetting. Offsetting a chain is NOT just offsetting
// each piece: the offset pieces no longer meet, and each corner must be healed.
//
//   * A corner where the offset pieces *converge* (an "internal" corner) is healed
//     by extending the two pieces' supports and trimming both to their crossing --
//     a sharp mitre.
//   * A corner where the offset pieces *diverge* (an "external" corner) leaves a gap
//     that must be bridged by an ARC centred on the original corner (radius = the
//     offset distance) -- a round join.
//
// Which case a corner is reduces to one sign: offsetting toward the inside of the
// turn converges (mitre), toward the outside diverges (arc). So the resulting chain
// generally has *more* pieces than the original (a round join per external corner).
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

        // Type probes / accessors. The chain holds only segments and arcs (ellipses
        // are tessellated to segments at build time, circles are their own loops).
        static bool isArc(const Stoicheion& e) { return std::string(e.kind()) == "arc"; }
        static const Segment2* asSeg(const Stoicheion& e) { return static_cast<const Segment2*>(&e); }
        static const Arc2*     asArc(const Stoicheion& e) { return static_cast<const Arc2*>(&e); }
        static Segment2* asSeg(Stoicheion& e) { return static_cast<Segment2*>(&e); }
        static Arc2*     asArc(Stoicheion& e) { return static_cast<Arc2*>(&e); }

        static Pos perpCCW(Pos v) { return Pos(-v.y, v.x); }   // +90 deg rotation

        static Pos eStart(const Stoicheion& e) { return isArc(e) ? Pos(asArc(e)->a) : Pos(asSeg(e)->a); }
        static Pos eEnd(const Stoicheion& e)   { return isArc(e) ? Pos(asArc(e)->b) : Pos(asSeg(e)->b); }

        // Unit tangent in the direction of travel, at the start / end.
        static Pos eStartDir(const Stoicheion& e) {
            if (isArc(e)) {
                const Arc2* a = asArc(e);
                Pos t = perpCCW(a->a - a->c);
                if (a->chirality() < 0) { t = t * -1.0f; }
                return t.normalized();
            }
            const Segment2* s = asSeg(e);
            return (s->b - s->a).normalized();
        }
        static Pos eEndDir(const Stoicheion& e) {
            if (isArc(e)) {
                const Arc2* a = asArc(e);
                Pos t = perpCCW(a->b - a->c);
                if (a->chirality() < 0) { t = t * -1.0f; }
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
        // an arc stays a concentric arc (left of CCW travel is inward).
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
            const Segment2* s = asSeg(e);
            Pos dir = s->b - s->a; float L = dir.pythag();
            if (L < 1e-9f) { return e.clone(); }
            Pos n = perpCCW(dir / L);                                   // left normal
            return std::make_unique<Segment2>(s->a + n * d, s->b + n * d);
        }

        // Trim an edge's start / end to a point (on its offset support).
        static void trimStart(Stoicheion& e, Pos p) {
            if (isArc(e)) { Arc2* a = asArc(e); float r = a->radius(); a->a = a->c + (p - a->c).normalized() * r; }
            else          { asSeg(e)->a = p; }
        }
        static void trimEnd(Stoicheion& e, Pos p) {
            if (isArc(e)) { Arc2* a = asArc(e); float r = a->radius(); a->b = a->c + (p - a->c).normalized() * r; }
            else          { asSeg(e)->b = p; }
        }

        // Support intersection (extended line / full circle), nearest to `hint`.
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
        static bool intersectSupports(const Stoicheion& A, const Stoicheion& B, Pos hint, Pos& out) {
            std::vector<Pos> cand;
            bool aa = isArc(A), ba = isArc(B);
            if (!aa && !ba) { Pos p; if (lineLineInf(eStart(A), eEnd(A), eStart(B), eEnd(B), p)) { cand.push_back(p); } }
            else if (!aa && ba) { lineCircleInf(eStart(A), eEnd(A), asArc(B)->c, asArc(B)->radius(), cand); }
            else if (aa && !ba) { lineCircleInf(eStart(B), eEnd(B), asArc(A)->c, asArc(A)->radius(), cand); }
            else                { circleCircle(asArc(A)->c, asArc(A)->radius(), asArc(B)->c, asArc(B)->radius(), cand); }
            if (cand.empty()) { return false; }
            float best = 1e30f;
            for (const Pos& p : cand) { float dd = (p - hint).pythag(); if (dd < best) { best = dd; out = p; } }
            return true;
        }

        // Winding: +1 CCW, -1 CW, 0 degenerate. Computed analytically -- the exact
        // signed area is half the sum of each edge's closed-form ∮(x dy - y dx) term
        // (Green's theorem), so an arc / circle / ellipse contributes its true area
        // with no tessellation or sampling anywhere.
        int windingSign() const {
            float twiceArea = 0.0f;
            for (const auto& e : edges) { twiceArea += e->signedAreaTerm(); }
            return (twiceArea > 1e-4f) ? 1 : (twiceArea < -1e-4f ? -1 : 0);
        }

        // The *raw* offset, done BLINDLY: every edge slides to the LEFT of its own
        // travel direction by `amount` -- no winding test, no preferred side. The
        // chain's chirality alone decides what that means: a CCW loop's interior is
        // on the left, so a positive amount insets it; a CW loop's interior is on the
        // right, so the very same operation offsets it outward. Inset vs outset is
        // emergent, never inferred. NOT pruned, so it may self-intersect; this is the
        // chain the debug view splits and colours, and offset() prunes.
        //
        // Lines only for now (segments in, segments + join arcs out); arcs, circles
        // and ellipses rejoin once the line case is settled.
        Chain offsetRaw(float amount) const {
            Chain result;
            if (edges.empty()) { return result; }
            result.closed = closed;

            // Lines-only guard: a chain holding anything we don't offset yet (a lone
            // circle / ellipse from build()) passes through unchanged.
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
            // mitre points, and the later chirality prune discards the wrong-handed
            // loops -- mitres, round joins, burrs and notches all out of one mechanism.
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

        // The pruned offset (rule 2): fracture the raw offset into simple loops and
        // keep only those whose winding matches the source -- the negative-area loops
        // that internal corners / pinched features produce are discarded.
        Chain offset(float amount) const {
            Chain raw = offsetRaw(amount);
            int srcW = windingSign();                          // source chirality
            if (!closed || srcW == 0) { return raw; }
            Chain kept; kept.closed = true;
            for (Chain& loop : raw.fractureLoops()) {
                if (loop.windingSign() == srcW) {              // keep loops matching the source
                    for (auto& e : loop.edges) { kept.edges.push_back(std::move(e)); }
                }
            }
            return kept;
        }

        // Healing -- rule 1: reverse tangencies.
        //--------------------------------------------------
        //
        // After insetting, a too-short line can get trimmed past itself and flip, so at
        // its junction with an adjoining arc it now points *opposite* the arc's tangent
        // -- a "reverse tangency", the line suddenly doubling back. Such a line is
        // spurious: it is deleted, and its other neighbour is re-joined to the orphaned
        // arc by extending to their support intersection. (Self-intersection cleanup --
        // rule 2 -- is a separate, later pass.)

        // Remove the first reverse-tangent line found; returns whether one was removed.
        bool healOneReverseTangency() {
            const size_t n = edges.size();
            if (n < 2) { return false; }
            constexpr float reverseDot = -0.5f;   // < this (~>120 deg) counts as a reversal

            for (size_t i = 0; i < n; i++) {
                if (isArc(*edges[i])) { continue; }   // the offender is a line
                int nextI = (i + 1 < n) ? static_cast<int>(i + 1) : (closed ? 0 : -1);
                int prevI = (i > 0)     ? static_cast<int>(i - 1) : (closed ? static_cast<int>(n - 1) : -1);

                bool reverse = false;
                if (nextI >= 0 && static_cast<size_t>(nextI) != i && isArc(*edges[nextI])) {
                    if (eEndDir(*edges[i]).dot(eStartDir(*edges[nextI])) < reverseDot) { reverse = true; }
                }
                if (prevI >= 0 && static_cast<size_t>(prevI) != i && isArc(*edges[prevI])) {
                    if (eEndDir(*edges[prevI]).dot(eStartDir(*edges[i])) < reverseDot) { reverse = true; }
                }
                if (!reverse) { continue; }

                // Re-join the line's two neighbours through the gap it leaves behind.
                if (prevI >= 0 && nextI >= 0 && prevI != nextI &&
                    static_cast<size_t>(prevI) != i && static_cast<size_t>(nextI) != i) {
                    Pos hint = (eStart(*edges[i]) + eEnd(*edges[i])) * 0.5f;
                    Pos x;
                    if (intersectSupports(*edges[prevI], *edges[nextI], hint, x)) {
                        trimEnd(*edges[prevI], x);
                        trimStart(*edges[nextI], x);
                    }
                }
                edges.erase(edges.begin() + i);
                return true;
            }
            return false;
        }

        void healReverseTangencies() {
            int guard = 0;
            while (guard++ < 1000 && healOneReverseTangency()) {}
        }

        // Fracture -- split self-intersections into simple loops, prune by winding.
        //--------------------------------------------------

        // True (with the parameter) when a point lies strictly inside a segment span.
        static bool onSeg(Pos a, Pos b, Pos p, float& t) {
            Pos d = b - a; float l2 = d.dot(d);
            if (l2 < 1e-12f) { return false; }
            t = (p - a).dot(d) / l2;
            return t > 1e-3f && t < 1.0f - 1e-3f;
        }
        // True when a point on an arc's circle lies strictly inside its swept range.
        static bool onArc(const Arc2& arc, Pos p) {
            float a0, sweep; arc.range(a0, sweep);
            float rel = wrapTau((p - arc.c).angle() - a0);
            return rel > 1e-3f && rel < sweep - 1e-3f;
        }

        // Genuine crossing points of two edges, strictly inside both spans.
        static void edgeCross(const Stoicheion& A, const Stoicheion& B, std::vector<Pos>& out) {
            bool aa = isArc(A), ba = isArc(B);
            if (!aa && !ba) {
                Pos p; if (!lineLineInf(eStart(A), eEnd(A), eStart(B), eEnd(B), p)) { return; }
                float t, u;
                if (onSeg(eStart(A), eEnd(A), p, t) && onSeg(eStart(B), eEnd(B), p, u)) { out.push_back(p); }
            }
            else if (!aa && ba) {
                std::vector<Pos> cand; lineCircleInf(eStart(A), eEnd(A), asArc(B)->c, asArc(B)->radius(), cand);
                for (Pos p : cand) { float t; if (onSeg(eStart(A), eEnd(A), p, t) && onArc(*asArc(B), p)) { out.push_back(p); } }
            }
            else if (aa && !ba) {
                std::vector<Pos> cand; lineCircleInf(eStart(B), eEnd(B), asArc(A)->c, asArc(A)->radius(), cand);
                for (Pos p : cand) { float t; if (onSeg(eStart(B), eEnd(B), p, t) && onArc(*asArc(A), p)) { out.push_back(p); } }
            }
            else {
                std::vector<Pos> cand; circleCircle(asArc(A)->c, asArc(A)->radius(), asArc(B)->c, asArc(B)->radius(), cand);
                for (Pos p : cand) { if (onArc(*asArc(A), p) && onArc(*asArc(B), p)) { out.push_back(p); } }
            }
        }

        // Split an edge at a point on it, preserving kind (a line into two lines, an
        // arc into two concentric arcs sharing the original chirality).
        static void splitEdge(const Stoicheion& e, Pos p,
                              std::unique_ptr<Stoicheion>& left, std::unique_ptr<Stoicheion>& right) {
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
                left  = std::make_unique<Arc2>(arc->c, Pos(arc->a), s, arc->c + Pos::fromAngle(midL) * r);
                right = std::make_unique<Arc2>(arc->c, s, Pos(arc->b), arc->c + Pos::fromAngle(midR) * r);
            }
            else {
                const Segment2* seg = asSeg(e);
                left  = std::make_unique<Segment2>(Pos(seg->a), p);
                right = std::make_unique<Segment2>(p, Pos(seg->b));
            }
        }

        struct Cross { Pos p; size_t i = 0, j = 0; };
        bool firstSelfCrossing(Cross& out) const {
            size_t n = edges.size();
            for (size_t i = 0; i < n; i++) {
                for (size_t j = i + 2; j < n; j++) {
                    if (closed && i == 0 && j == n - 1) { continue; }    // adjacent across the wrap
                    std::vector<Pos> pts; edgeCross(*edges[i], *edges[j], pts);
                    if (!pts.empty()) { out = { pts[0], i, j }; return true; }
                }
            }
            return false;
        }

        // *Every* genuine self-crossing point (including between adjacent edges -- two
        // touching arcs can still cross a second time, which the pinch-split skips).
        // Diagnostic: yellow dots that don't line up with a split reveal the bug.
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

        // Split this (self-intersecting) chain into the minimal set of simple loops:
        // at the first crossing, pinch into the inner span and the remainder, each
        // closed at the crossing point, and recurse. A simple chain returns unchanged.
        std::vector<Chain> splitSimpleLoops(int budget = 512) const {
            Cross x;
            if (budget <= 0 || !firstSelfCrossing(x)) {
                std::vector<Chain> r; r.push_back(clone()); return r;
            }
            std::unique_ptr<Stoicheion> iLeft, iRight, jLeft, jRight;
            splitEdge(*edges[x.i], x.p, iLeft, iRight);
            splitEdge(*edges[x.j], x.p, jLeft, jRight);

            Chain a; a.closed = true;                                    // inner: iRight, (i+1..j-1), jLeft
            a.edges.push_back(std::move(iRight));
            for (size_t k = x.i + 1; k < x.j; k++) { a.edges.push_back(edges[k]->clone()); }
            a.edges.push_back(std::move(jLeft));

            Chain b; b.closed = true;                                    // outer: jRight, (j+1..end, 0..i-1), iLeft
            b.edges.push_back(std::move(jRight));
            for (size_t k = x.j + 1; k < edges.size(); k++) { b.edges.push_back(edges[k]->clone()); }
            for (size_t k = 0; k < x.i; k++) { b.edges.push_back(edges[k]->clone()); }
            b.edges.push_back(std::move(iLeft));

            std::vector<Chain> out;
            for (Chain& s : a.splitSimpleLoops(budget - 1)) { out.push_back(std::move(s)); }
            for (Chain& s : b.splitSimpleLoops(budget - 1)) { out.push_back(std::move(s)); }
            return out;
        }

        // Parameter (0..1) of a point along an edge (for ordering crossings on it).
        static float paramOnEdge(const Stoicheion& e, Pos p) {
            if (isArc(e)) {
                const Arc2* a = asArc(e);
                float a0, sweep; a->range(a0, sweep);
                if (sweep < 1e-9f) { return 0.0f; }
                return wrapTau((p - a->c).angle() - a0) / sweep;
            }
            Pos s = eStart(e), en = eEnd(e);
            Pos d = en - s; float l2 = d.dot(d);
            return (l2 > 1e-12f) ? (p - s).dot(d) / l2 : 0.0f;
        }

        // Split an edge at every point on it (sorted along the edge) into sub-edges.
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
                std::unique_ptr<Stoicheion> left, right;
                splitEdge(*rem, pr.second, left, right);
                out.push_back(std::move(left));
                rem = std::move(right);
                lastT = pr.first;
            }
            out.push_back(std::move(rem));
        }

        // The arrangement fracture (the right way): find ALL self-intersections, split
        // EVERY edge at all of its crossings, then trace the simple loops out of the
        // resulting sub-edges. A loop closes whenever the walk returns to a vertex it
        // already passed through -- so all the cuts are identified first, then made,
        // then the loops are traced, exactly as intended.
        std::vector<Chain> fractureLoops(float eps = 1e-3f) const {
            size_t n = edges.size();

            // 1. all crossing points, gathered per edge.
            std::vector<std::vector<Pos>> pts(n);
            for (size_t i = 0; i < n; i++) {
                for (size_t j = i + 1; j < n; j++) {
                    std::vector<Pos> c; edgeCross(*edges[i], *edges[j], c);
                    for (const Pos& p : c) { pts[i].push_back(p); pts[j].push_back(p); }
                }
            }

            // 2. split every edge at its crossings -> the fine sequence, in chain order.
            std::vector<std::unique_ptr<Stoicheion>> fine;
            for (size_t i = 0; i < n; i++) { splitEdgeAtPoints(*edges[i], pts[i], fine); }

            // 3. trace simple loops: pop a loop off whenever we return to an earlier vertex.
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
}
