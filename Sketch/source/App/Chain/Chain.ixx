module;

#include <vector>
#include <memory>
#include <string>
#include <cmath>
#include <algorithm>

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

        // Winding (sampled signed area): +1 CCW, -1 CW, 0 degenerate.
        int windingSign() const {
            std::vector<Pos> pts;
            for (const auto& e : edges) { std::vector<Pos> p; e->tessellate(p); for (const Pos& q : p) { pts.push_back(q); } }
            if (pts.size() < 3) { return 0; }
            float area = 0.0f;
            for (size_t i = 0; i < pts.size(); i++) { const Pos& a = pts[i]; const Pos& b = pts[(i + 1) % pts.size()]; area += a.x * b.y - b.x * a.y; }
            return (area > 1e-4f) ? 1 : (area < -1e-4f ? -1 : 0);
        }

        // Offset the whole chain by `amount` (negative = inset toward the interior),
        // healing every corner. The result is a fresh chain, generally with more
        // pieces (a round join inserted at each external corner).
        Chain offset(float amount) const {
            Chain result;
            if (edges.empty()) { return result; }
            result.closed = closed;

            // A lone closed circle just offsets concentrically -- no corners.
            if (edges.size() == 1 && std::string(edges.front()->kind()) == "circle") {
                result.edges.push_back(edges.front()->offsetBy(amount));
                return result;
            }

            int w = windingSign(); if (w == 0) { w = 1; }
            const float leftAmount = -amount * static_cast<float>(w);   // inset -> toward interior side

            std::vector<std::unique_ptr<Stoicheion>> off;
            off.reserve(edges.size());
            for (const auto& e : edges) { off.push_back(offsetLeft(*e, leftAmount)); }

            const size_t n = off.size();
            const size_t corners = closed ? n : (n > 0 ? n - 1 : 0);

            // Classify each corner; gather mitre points and round-join arcs.
            std::vector<int> kind(corners, 0);                 // 0 = trim/mitre, 1 = arc, 2 = bridge
            std::vector<Pos> point(corners);
            std::vector<std::unique_ptr<Stoicheion>> joinArc(corners);

            for (size_t k = 0; k < corners; k++) {
                Stoicheion& A = *off[k];
                Stoicheion& B = *off[(k + 1) % n];
                Pos corner = eEnd(*edges[k]);                  // original (un-offset) corner
                Pos endA = eEnd(A), startB = eStart(B);
                Pos tanA = eEndDir(A), tanB = eStartDir(B);
                float turn = tanA.cross(tanB);

                if ((endA - startB).pythag() < 1e-4f) {
                    kind[k] = 3;                                       // already coincident: do nothing
                }
                else if (std::fabs(turn) < 1e-4f) {
                    kind[k] = 2; point[k] = (endA + startB) * 0.5f;    // collinear with a gap: bridge
                }
                else if (leftAmount * turn > 0.0f) {
                    Pos x;                                             // converging: clip to crossing
                    if (intersectSupports(A, B, corner, x)) { kind[k] = 0; point[k] = x; }
                    else { kind[k] = 2; point[k] = (endA + startB) * 0.5f; }
                }
                else {
                    kind[k] = 1;                                       // diverging: tangent arc join
                    joinArc[k] = std::make_unique<Arc2>(Arc2::Tangent(endA, startB, tanA, tanB));
                }
            }

            // Apply the mitre / bridge trims (arc corners keep their offset endpoints).
            for (size_t k = 0; k < corners; k++) {
                if (kind[k] == 0 || kind[k] == 2) {
                    trimEnd(*off[k], point[k]);
                    trimStart(*off[(k + 1) % n], point[k]);
                }
            }

            // Assemble: each edge, followed by its corner's round join when present.
            for (size_t i = 0; i < n; i++) {
                result.edges.push_back(std::move(off[i]));
                bool hasCorner = closed || (i + 1 < n);
                if (hasCorner && i < corners && kind[i] == 1) { result.edges.push_back(std::move(joinArc[i])); }
            }

            result.healReverseTangencies();   // rule 1: drop lines that flipped on themselves
            return result;
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
        // circle is its own closed loop; ellipses are tessellated to segments first;
        // datums / construction / points are ignored.
        static std::vector<Chain> build(const std::vector<std::unique_ptr<Stoicheion>>& src, float eps = 1e-3f) {
            std::vector<Chain> out;
            std::vector<std::unique_ptr<Stoicheion>> pool;

            for (const auto& e : src) {
                if (!e || e->locked || e->construction || e->isPoint()) { continue; }
                std::string k = e->kind();
                if (k == "circle") { Chain c; c.closed = true; c.edges.push_back(e->clone()); out.push_back(std::move(c)); }
                else if (k == "segment" || k == "arc") { pool.push_back(e->clone()); }
                else {                                                    // ellipse &c -> polyline
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
