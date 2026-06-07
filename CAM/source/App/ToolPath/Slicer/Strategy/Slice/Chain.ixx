module;

#include <vector>
#include <cstddef>
#include <cmath>
#include <algorithm>

#include <dbg.hpp>

export module Cam.App.Slicer.Strategy.Slice.Chain;

import Rev.Core.Pos;

import Cam.App.Slicer.Strategy.Slice.Segment2;

export namespace Cam::App::Slicer::Strategy::Slice {

    using namespace Rev::Core;

    struct Chain {

        std::vector<Segment> segments;

        // Create
        //--------------------------------------------------

        Chain() {}

        Chain(const std::vector<Segment>& source) {
            build(source);
        }

        static Chain From(const std::vector<Segment>& source) {
            return Chain(source);
        }

        static std::vector<Chain> BuildAll(const std::vector<Segment>& source, float eps = 1e-4f) {
            std::vector<Segment> remaining = source;
            std::vector<Chain> out;

            while (!remaining.empty()) {

                Chain chain;

                chain.buildOneFromRemaining(remaining, eps);

                if (chain.empty()) { break; }

                out.push_back(chain);
            }

            return out;
        }

        // State
        //--------------------------------------------------

        void clear() {
            segments.clear();
        }

        bool empty() const {
            return segments.empty();
        }

        size_t size() const {
            return segments.size();
        }

        void push(const Segment& s) {
            segments.push_back(s);
        }

        // Build
        //--------------------------------------------------

        void build(const std::vector<Segment>& source, float eps = 1e-4f) {
            clear();

            std::vector<Segment> remaining = source;

            buildOneFromRemaining(remaining, eps);
        }

        void buildOneFromRemaining(std::vector<Segment>& remaining, float eps = 1e-4f) {
            clear();

            if (remaining.empty()) { return; }

            push(remaining.front());
            remaining.erase(remaining.begin());

            Pos loopStart = start();

            while (!remaining.empty()) {

                size_t best = 0;
                bool reverseSegment = false;
                bool found = false;
                float bestDistance = 0.0f;

                Pos current = end();

                for (size_t i = 0; i < remaining.size(); i++) {

                    float dStart = current.distanceTo(remaining[i].start());
                    float dEnd = current.distanceTo(remaining[i].end());

                    if (!found || dStart < bestDistance) {
                        best = i;
                        reverseSegment = false;
                        bestDistance = dStart;
                        found = true;
                    }

                    if (dEnd < bestDistance) {
                        best = i;
                        reverseSegment = true;
                        bestDistance = dEnd;
                        found = true;
                    }
                }

                if (!found) { break; }
                if (bestDistance > eps) { break; }

                Segment next = reverseSegment ? remaining[best].reversed() : remaining[best];

                push(next);
                remaining.erase(remaining.begin() + best);

                if (end().distanceTo(loopStart) <= eps) {
                    segments.back().setEnd(loopStart);
                    break;
                }
            }
        }

        // Inspect
        //--------------------------------------------------

        Pos start() const {
            if (empty()) { return Pos::Invalid(); }
            return segments.front().start();
        }

        Pos end() const {
            if (empty()) { return Pos::Invalid(); }
            return segments.back().end();
        }

        bool closed(float eps = 1e-4f) const {
            if (empty()) { return false; }
            return start().distanceTo(end()) <= eps;
        }

        bool adjacent(size_t i, size_t j) const {

            if (i == j) { return true; }
            if (i + 1 == j || j + 1 == i) { return true; }

            if (segments.size() <= 2) { return true; }

            if (closed()) {
                if (i == 0 && j + 1 == segments.size()) { return true; }
                if (j == 0 && i + 1 == segments.size()) { return true; }
            }

            return false;
        }

        Pos min() const {

            if (empty()) { return Pos::Invalid(); }

            Pos out = segments.front().min();

            for (const Segment& s : segments) {
                out = Pos::min(out, s.min());
                out = Pos::min(out, s.max());
            }

            return out;
        }

        Pos max() const {

            if (empty()) { return Pos::Invalid(); }

            Pos out = segments.front().max();

            for (const Segment& s : segments) {
                out = Pos::max(out, s.min());
                out = Pos::max(out, s.max());
            }

            return out;
        }

        float boundsArea() const {

            if (empty()) { return 0.0f; }

            Pos mn = min();
            Pos mx = max();

            return std::abs((mx.x - mn.x) * (mx.y - mn.y));
        }

        // Winding
        //--------------------------------------------------

        float signedArea(int samplesPerSegment = 8) const {
            if (segments.empty()) { return 0.0f; }

            std::vector<Pos> pts;

            sample(pts, samplesPerSegment);

            if (pts.size() < 3) { return 0.0f; }

            float area = 0.0f;

            for (size_t i = 0; i < pts.size(); i++) {

                const Pos& a = pts[i];
                const Pos& b = pts[(i + 1) % pts.size()];

                area += (a.x * b.y - b.x * a.y);
            }

            return area * 0.5f;
        }

        int windingSign(float eps = 1e-4f) const {

            float a = signedArea();

            if (a > eps) { return 1; }
            if (a < -eps) { return -1; }

            return 0;
        }

        bool counterClockwise() const {
            return windingSign() > 0;
        }

        bool clockwise() const {
            return windingSign() < 0;
        }

        bool zeroWinding() const {
            return windingSign() == 0;
        }

        void reverse() {

            std::vector<Segment> out;

            for (size_t i = segments.size(); i > 0; i--) {
                out.push_back(segments[i - 1].reversed());
            }

            segments = out;
        }

        void forceCounterClockwise() {
            if (clockwise()) { reverse(); }
        }

        void forceClockwise() {
            if (counterClockwise()) { reverse(); }
        }

        // Directional normals
        //--------------------------------------------------

        Pos leftNormalAt(size_t segmentIndex, float t = 0.5f) const {
            if (segmentIndex >= segments.size()) {
                return { 0.0f, 0.0f };
            }

            Pos tangent = segments[segmentIndex].tangentAt(t);

            return {
                -tangent.y,
                tangent.x
            };
        }

        Pos rightNormalAt(size_t segmentIndex, float t = 0.5f) const {
            return leftNormalAt(segmentIndex, t) * -1.0f;
        }

        Pos interiorNormalAt(size_t segmentIndex, float t = 0.5f) const {
            // For a CCW closed chain, interior is left.
            // For a CW closed chain, interior is right.
            if (counterClockwise()) {
                return leftNormalAt(segmentIndex, t);
            }

            if (clockwise()) {
                return rightNormalAt(segmentIndex, t);
            }

            return { 0.0f, 0.0f };
        }

        Pos exteriorNormalAt(size_t segmentIndex, float t = 0.5f) const {
            return interiorNormalAt(segmentIndex, t) * -1.0f;
        }

        // Offsetting
        //--------------------------------------------------

        Segment translatedSegment(const Segment& s, const Pos& offset) const {
            switch (s.kind) {

                case Segment::Kind::Line: {
                    return Segment::Line(s.p0 + offset, s.p1 + offset);
                }

                case Segment::Kind::Arc: {
                    return Segment::Arc(s.p0 + offset, s.f0, s.f1, s.f2);
                }

                case Segment::Kind::Bezier: {
                    return Segment::Bezier(s.p0 + offset, s.p1 + offset, s.p2 + offset, s.p3 + offset);
                }

                default: {
                    return s;
                }
            }
        }

        Segment offsetSegmentNormal(const Segment& s, float amount, bool towardInterior) const {
            switch (s.kind) {

                case Segment::Kind::Arc: {
                    Pos n = towardInterior
                        ? interiorNormalAtSegment(s)
                        : exteriorNormalAtSegment(s);

                    Pos radial = s.pointAt(0.5f) - s.p0;
                    float radialLength = radial.pythag();

                    if (radialLength <= 1e-8f) {
                        return s;
                    }

                    radial = radial * (1.0f / radialLength);

                    float radius = s.f0 + amount * (
                        n.dot(radial) >= 0.0f
                            ? 1.0f
                            : -1.0f
                    );

                    if (radius <= 1e-6f) {
                        return Segment::Line(s.p0, s.p0);
                    }

                    return Segment::Arc(s.p0, radius, s.f1, s.f2);
                }

                default: {
                    Pos n = towardInterior
                        ? interiorNormalAtSegment(s)
                        : exteriorNormalAtSegment(s);

                    return translatedSegment(s, n * amount);
                }
            }
        }

        Pos interiorNormalAtSegment(const Segment& s, float t = 0.5f) const {
            Pos tangent = s.tangentAt(t);

            Pos left = {
                -tangent.y,
                tangent.x
            };

            if (counterClockwise()) {
                return left;
            }

            if (clockwise()) {
                return left * -1.0f;
            }

            return { 0.0f, 0.0f };
        }

        Pos exteriorNormalAtSegment(const Segment& s, float t = 0.5f) const {
            return interiorNormalAtSegment(s, t) * -1.0f;
        }

        // Uniform offsets are just the per-segment offset with one shared
        // amount, so they inherit the same arc-aware support joining (extend
        // each segment's support, intersect consecutive supports, adopt the
        // nearest crossing as the corner).  This is what keeps offset arcs
        // properly trimmed to their neighbors instead of floating free.
        Chain offsetInterior(float amount) const {
            return insetPerSegment(std::vector<float>(segments.size(), amount));
        }

        Chain offsetExterior(float amount) const {
            return insetPerSegment(std::vector<float>(segments.size(), -amount));
        }

        // Offset each segment by its own signed amount, preserving segment
        // count, type, and direction.  A positive amount moves a segment toward
        // the chain interior (inset); a negative amount moves it outward
        // (outset).  Lines stay parallel lines, arcs stay concentric arcs.
        //
        // Because neighbors may move in opposite directions, the offset pieces
        // no longer touch.  Rather than trim within each segment's original
        // span (which fails when the true corner lies *beyond* the offset
        // pieces), we extend each segment's support — the infinite line for a
        // line, the full circle for an arc — intersect consecutive supports,
        // and adopt the intersection nearest the gap as the new shared corner.
        // This reconstructs a clean ring with exactly the original segments.
        Chain insetPerSegment(const std::vector<float>& amounts) const {

            Chain out;

            if (empty()) { return out; }

            const size_t n = segments.size();

            std::vector<Segment> off(n);

            for (size_t i = 0; i < n; i++) {

                const float amount = i < amounts.size() ? amounts[i] : 0.0f;

                off[i] = offsetSegmentSigned(segments[i], amount);
            }

            const bool isClosed = closed();
            const size_t joinCount = isClosed ? n : (n > 0 ? n - 1 : 0);

            // Resolve every corner from the *un-mutated* offset supports first,
            // so each join is independent of the order we apply them in.
            std::vector<Pos> corners(joinCount);

            for (size_t k = 0; k < joinCount; k++) {

                const Segment& a = off[k];
                const Segment& b = off[(k + 1) % n];

                std::vector<Pos> candidates;
                supportIntersections(a, b, candidates);

                // Fallback if the supports don't meet: bridge the gap midpoint.
                Pos chosen = (a.end() + b.start()) * 0.5f;
                float bestCost = 0.0f;
                bool found = false;

                for (const Pos& c : candidates) {

                    // A line/circle (or circle/circle) pair yields two crossings;
                    // we must pick the one that continues each segment in the
                    // direction it actually runs, not the far side.  In parameter
                    // space the natural corner sits just past a's end (t = 1) and
                    // just before b's start (t = 0), so the right crossing is the
                    // one minimizing the parametric travel from those endpoints.
                    float ta = 0.0f;
                    float tb = 0.0f;

                    float cost;

                    if (paramOnSegment(a, c, ta) && paramOnSegment(b, c, tb)) {
                        cost = std::abs(ta - 1.0f) + std::abs(tb);
                    }
                    else {
                        cost = c.distanceTo(chosen);
                    }

                    if (!found || cost < bestCost) {
                        chosen = c;
                        bestCost = cost;
                        found = true;
                    }
                }

                corners[k] = chosen;
            }

            for (size_t k = 0; k < joinCount; k++) {

                setSegmentEnd(off[k], corners[k]);
                setSegmentStart(off[(k + 1) % n], corners[k]);
            }

            out.segments = off;

            return out;
        }

        // Signed single-segment offset: positive toward interior, negative
        // toward exterior.
        Segment offsetSegmentSigned(const Segment& s, float interiorAmount) const {
            return offsetSegmentNormal(s, std::abs(interiorAmount), interiorAmount >= 0.0f);
        }

        // Support intersection
        //--------------------------------------------------

        // All intersection points of the two segments' extended supports: line
        // segments extend to their infinite line, arcs to their full circle.
        static void supportIntersections(const Segment& a, const Segment& b, std::vector<Pos>& out) {

            const bool aArc = a.kind == Segment::Kind::Arc;
            const bool bArc = b.kind == Segment::Kind::Arc;

            if (!aArc && !bArc) {
                Pos p;
                if (lineLineInfinite(a.start(), a.end(), b.start(), b.end(), p)) {
                    out.push_back(p);
                }
                return;
            }

            if (!aArc && bArc) {
                lineCircleInfinite(a.start(), a.end(), b.p0, b.f0, out);
                return;
            }

            if (aArc && !bArc) {
                lineCircleInfinite(b.start(), b.end(), a.p0, a.f0, out);
                return;
            }

            circleCircle(a.p0, a.f0, b.p0, b.f0, out);
        }

        static bool lineLineInfinite(const Pos& a0, const Pos& a1, const Pos& b0, const Pos& b1, Pos& out) {

            Pos r = a1 - a0;
            Pos s = b1 - b0;

            float rxs = r.cross(s);

            if (std::abs(rxs) <= 1e-9f) { return false; }

            float t = (b0 - a0).cross(s) / rxs;

            out = a0 + r * t;

            return true;
        }

        static void lineCircleInfinite(const Pos& a0, const Pos& a1, const Pos& center, float radius, std::vector<Pos>& out) {

            Pos d = a1 - a0;

            float aa = d.dot(d);

            if (aa <= 1e-12f) { return; }

            Pos f = a0 - center;

            float bb = 2.0f * f.dot(d);
            float cc = f.dot(f) - radius * radius;

            float disc = bb * bb - 4.0f * aa * cc;

            if (disc < 0.0f) { return; }

            disc = std::sqrt(disc);

            float t0 = (-bb - disc) / (2.0f * aa);
            float t1 = (-bb + disc) / (2.0f * aa);

            out.push_back(a0 + d * t0);

            if (disc > 1e-9f) {
                out.push_back(a0 + d * t1);
            }
        }

        static void circleCircle(const Pos& c0, float r0, const Pos& c1, float r1, std::vector<Pos>& out) {

            Pos d = c1 - c0;

            float dist = d.pythag();

            if (dist <= 1e-9f) { return; }
            if (dist > r0 + r1 + 1e-6f) { return; }
            if (dist < std::abs(r0 - r1) - 1e-6f) { return; }

            float a = (r0 * r0 - r1 * r1 + dist * dist) / (2.0f * dist);
            float h2 = r0 * r0 - a * a;

            if (h2 < 0.0f) { h2 = 0.0f; }

            float h = std::sqrt(h2);

            Pos mid = c0 + d * (a / dist);

            Pos perp = { -d.y / dist * h, d.x / dist * h };

            out.push_back(mid + perp);

            if (h > 1e-9f) {
                out.push_back(mid - perp);
            }
        }

        // Endpoint setters that keep arcs valid: an arc's endpoint is its angle,
        // so we recompute the angle and pick the representative nearest the old
        // one to preserve sweep direction.
        static void setSegmentEnd(Segment& s, const Pos& p) {
            if (s.kind == Segment::Kind::Arc) {
                s.f2 = nearestAngle(std::atan2(p.y - s.p0.y, p.x - s.p0.x), s.f2);
                return;
            }
            s.setEnd(p);
        }

        static void setSegmentStart(Segment& s, const Pos& p) {
            if (s.kind == Segment::Kind::Arc) {
                s.f1 = nearestAngle(std::atan2(p.y - s.p0.y, p.x - s.p0.x), s.f1);
                return;
            }
            s.setStart(p);
        }

        static float nearestAngle(float angle, float reference) {
            const float pi = 3.14159265358979f;
            const float twoPi = 2.0f * pi;

            while (angle - reference > pi) { angle -= twoPi; }
            while (reference - angle > pi) { angle += twoPi; }

            return angle;
        }

        // inset = toward chain interior.
        // outset = away from chain interior.
        Chain inset(float amount) const {
            return offsetInterior(amount);
        }

        Chain outset(float amount) const {
            return offsetExterior(amount);
        }

        // Relinking
        //--------------------------------------------------

        void healSelfIntersections() {

            if (segments.size() < 4) { return; }

            for (size_t i = 0; i < segments.size(); i++) {

                for (size_t j = i + 1; j < segments.size(); j++) {

                    if (adjacent(i, j)) { continue; }

                    Pos p = segments[i].intersection(segments[j]);

                    if (!p) { continue; }

                    segments[i] = Segment::Line(p, p);
                    segments[j] = Segment::Line(p, p);
                }
            }
        }

        // Degeneracy
        //--------------------------------------------------

        static float orient(const Pos& a, const Pos& b, const Pos& c) {
            return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        }

        // Proper crossing of segments (p1,p2) and (p3,p4): strict opposite
        // orientations on both sides, so shared endpoints / collinear touches
        // (legitimate at chain joins) are not counted.
        static bool segmentsCross(
            const Pos& p1, const Pos& p2,
            const Pos& p3, const Pos& p4
        ) {
            float d1 = orient(p3, p4, p1);
            float d2 = orient(p3, p4, p2);
            float d3 = orient(p1, p2, p3);
            float d4 = orient(p1, p2, p4);

            return ((d1 > 0.0f && d2 < 0.0f) || (d1 < 0.0f && d2 > 0.0f)) &&
                   ((d3 > 0.0f && d4 < 0.0f) || (d3 < 0.0f && d4 > 0.0f));
        }

        // Robust self-intersection test on the chain's sampled polyline.  This
        // catches both crossings between distinct segments AND fold-back spikes
        // between adjacent segments (the fold shows up as a crossing between
        // non-consecutive sampled edges).  Exact for line offsets; sampling
        // resolves arcs/beziers.  We can afford the O(n^2) sweep — paths
        // generate near-instantly.
        bool selfIntersects(float eps = 1e-4f) const {

            if (segments.size() < 2) { return false; }

            std::vector<Pos> pts;
            sample(pts, 4);

            const bool isClosed = closed(eps);

            // Drop the duplicated closing point so the wrap edge is genuine.
            if (
                isClosed &&
                pts.size() >= 2 &&
                pts.front().distanceTo(pts.back()) <= eps
            ) {
                pts.pop_back();
            }

            const size_t n = pts.size();

            if (n < 4) { return false; }

            const size_t edgeCount = isClosed ? n : (n - 1);

            for (size_t i = 0; i < edgeCount; i++) {

                const Pos& a0 = pts[i];
                const Pos& a1 = pts[(i + 1) % n];

                // j starts at i+2 so we never test consecutive (vertex-sharing) edges.
                for (size_t j = i + 2; j < edgeCount; j++) {

                    // Skip the wrap pair that shares vertex 0 on a closed loop.
                    if (isClosed && i == 0 && j == edgeCount - 1) { continue; }

                    const Pos& b0 = pts[j];
                    const Pos& b1 = pts[(j + 1) % n];

                    if (segmentsCross(a0, a1, b0, b1)) {
                        return true;
                    }
                }
            }

            return false;
        }

        // An offset chain is degenerate once it has folded over itself or
        // collapsed to ~zero area.  Self-intersection is judged regardless of
        // closure: the naive offset can drift the endpoints apart so a folded
        // ring reads as "open", and that must NOT hide the fold.
        bool degenerate(float eps = 1e-4f) const {

            if (empty()) { return true; }
            if (selfIntersects(eps)) { return true; }
            if (closed(eps) && std::abs(signedArea()) <= eps) { return true; }

            return false;
        }

        // Parametric position (0..1) of a point known to lie on a segment's
        // support.  Lines project onto the chord; arcs convert the polar angle
        // back to sweep fraction.  Used to split a segment exactly at a crossing.
        static bool paramOnSegment(const Segment& s, const Pos& p, float& t) {
            switch (s.kind) {

                case Segment::Kind::Arc: {

                    float span = s.f2 - s.f1;

                    if (std::abs(span) <= 1e-9f) { return false; }

                    float angle = nearestAngle(std::atan2(p.y - s.p0.y, p.x - s.p0.x), s.f1);

                    t = (angle - s.f1) / span;

                    return true;
                }

                default: {

                    Pos ab = s.end() - s.start();

                    float len2 = ab.dot(ab);

                    if (len2 <= 1e-12f) { return false; }

                    t = (p - s.start()).dot(ab) / len2;

                    return true;
                }
            }
        }

        // Split a segment at parameter t into its two halves, preserving kind:
        // a line splits into two lines, an arc into two concentric arcs (split
        // angle), a bezier via de Casteljau.
        static void splitSegmentAt(const Segment& s, float t, Segment& left, Segment& right) {
            switch (s.kind) {

                case Segment::Kind::Arc: {

                    float at = s.f1 + (s.f2 - s.f1) * t;

                    left = Segment::Arc(s.p0, s.f0, s.f1, at);
                    right = Segment::Arc(s.p0, s.f0, at, s.f2);

                    return;
                }

                case Segment::Kind::Bezier: {

                    Pos p01 = s.p0 + (s.p1 - s.p0) * t;
                    Pos p12 = s.p1 + (s.p2 - s.p1) * t;
                    Pos p23 = s.p2 + (s.p3 - s.p2) * t;

                    Pos p012 = p01 + (p12 - p01) * t;
                    Pos p123 = p12 + (p23 - p12) * t;

                    Pos mid = p012 + (p123 - p012) * t;

                    left = Segment::Bezier(s.p0, p01, p012, mid);
                    right = Segment::Bezier(mid, p123, p23, s.p3);

                    return;
                }

                default: {

                    Pos p = s.pointAt(t);

                    left = Segment::Line(s.start(), p);
                    right = Segment::Line(p, s.end());

                    return;
                }
            }
        }

        // A self-crossing between two non-adjacent segments: the exact crossing
        // point and the parameter on each segment.
        struct SelfCrossing {
            Pos p;
            size_t i = 0;
            size_t j = 0;
            float ti = 0.0f;
            float tj = 0.0f;
        };

        // Find the first genuine crossing between two non-adjacent segments,
        // using exact segment-vs-segment geometry (extended supports intersected,
        // then validated to lie strictly inside both spans).  Unlike the sampled
        // selfIntersects() this returns where to cut, not merely whether to.
        bool firstSelfCrossing(SelfCrossing& out, float eps = 1e-4f) const {

            const size_t n = segments.size();

            if (n < 2) { return false; }

            const bool isClosed = closed(eps);

            for (size_t i = 0; i < n; i++) {

                for (size_t j = i + 2; j < n; j++) {

                    // seg 0 and seg n-1 share a vertex on a closed loop.
                    if (isClosed && i == 0 && j == n - 1) { continue; }

                    std::vector<Pos> candidates;
                    supportIntersections(segments[i], segments[j], candidates);

                    for (const Pos& c : candidates) {

                        float ti = 0.0f;
                        float tj = 0.0f;

                        if (!paramOnSegment(segments[i], c, ti)) { continue; }
                        if (!paramOnSegment(segments[j], c, tj)) { continue; }

                        if (ti <= eps || ti >= 1.0f - eps) { continue; }
                        if (tj <= eps || tj >= 1.0f - eps) { continue; }

                        out = { c, i, j, ti, tj };

                        return true;
                    }
                }
            }

            return false;
        }

        // Fractalize: split a self-intersecting chain into the minimum number of
        // simple closed sub-loops that, drawn together, are indistinguishable
        // from the original.  At the first crossing the loop is pinched in two —
        // the span between the crossing edges, and the remainder — both closed
        // at the crossing point, and each is recursed on until simple.
        //
        // Geometry is preserved exactly: the segments on either side of the cut
        // keep their type (arcs stay arcs) and only the two crossing segments
        // are split, so the loop count stays minimal and segments do not
        // multiply.  A simple chain is returned unchanged.  Orientation/area
        // filtering (dropping inverted pinch lobes) is left to the caller.
        std::vector<Chain> splitSimpleLoops(float eps = 1e-4f, int budget = 512) const {

            SelfCrossing x;

            if (budget <= 0 || !firstSelfCrossing(x, eps)) {
                return { *this };
            }

            Segment iLeft, iRight, jLeft, jRight;

            splitSegmentAt(segments[x.i], x.ti, iLeft, iRight);
            splitSegmentAt(segments[x.j], x.tj, jLeft, jRight);

            // Pin all four cut ends exactly onto the crossing point.
            setSegmentEnd(iLeft, x.p);
            setSegmentStart(iRight, x.p);
            setSegmentEnd(jLeft, x.p);
            setSegmentStart(jRight, x.p);

            // Inner loop: iRight, segs (i+1 .. j-1), jLeft.  Closes at x.p.
            Chain loopA;
            loopA.push(iRight);
            for (size_t k = x.i + 1; k < x.j; k++) { loopA.push(segments[k]); }
            loopA.push(jLeft);

            // Outer loop: jRight, segs (j+1 .. n-1), segs (0 .. i-1), iLeft.
            Chain loopB;
            loopB.push(jRight);
            for (size_t k = x.j + 1; k < segments.size(); k++) { loopB.push(segments[k]); }
            for (size_t k = 0; k < x.i; k++) { loopB.push(segments[k]); }
            loopB.push(iLeft);

            std::vector<Chain> out;

            for (const Chain& part : loopA.splitSimpleLoops(eps, budget - 1)) {
                out.push_back(part);
            }

            for (const Chain& part : loopB.splitSimpleLoops(eps, budget - 1)) {
                out.push_back(part);
            }

            return out;
        }

        // Concentric clearing (depth-first)
        //--------------------------------------------------

        // Repeatedly offset this ring — inward when `interior`, outward
        // otherwise — emitting each clean ring into `out`.  When an offset
        // self-intersects, fracture it into minimal simple loops (geometry
        // preserved); each genuine loop (winding matching `windingRef`, area
        // above `minArea`) is emitted and then cleared recursively, depth-first.
        // Fracturing is capped at `maxSplitDepth` levels so a pathological
        // region cannot fragment without bound.
        void gatherConcentric(
            std::vector<Chain>& out,
            float step,
            float minArea,
            bool interior,
            int windingRef,
            int splitDepth,
            int maxSplitDepth,
            int maxPasses = 256
        ) const {

            Chain current = *this;

            for (int pass = 0; pass < maxPasses; pass++) {

                Chain next = interior
                    ? current.offsetInterior(step)
                    : current.offsetExterior(step);

                if (next.empty()) { return; }

                if (next.selfIntersects()) {

                    // Out of fracture budget — stop descending this branch.
                    if (splitDepth >= maxSplitDepth) { return; }

                    for (Chain& loop : next.splitSimpleLoops()) {

                        if (loop.size() < 2) { continue; }

                        const float area = loop.signedArea();

                        if (std::abs(area) <= minArea) { continue; }   // scrap

                        const int sign = area > 0.0f ? 1 : -1;

                        // Drop inverted pinch lobes (opposite the source winding).
                        if (windingRef != 0 && sign != windingRef) { continue; }

                        out.push_back(loop);

                        loop.gatherConcentric(
                            out, step, minArea, interior, windingRef,
                            splitDepth + 1, maxSplitDepth, maxPasses
                        );
                    }

                    return;
                }

                // Clean ring: a collapsed (~zero area) ring is terminal.
                if (next.closed() && std::abs(next.signedArea()) <= minArea) {
                    return;
                }

                out.push_back(next);

                // Too little left to host another distinct pass.
                if (std::abs(next.signedArea()) <= minArea) { return; }

                current = next;
            }
        }

        // Sampling
        //--------------------------------------------------

        void sample(std::vector<Pos>& out, int samplesPerSegment = 8) const {
            out.clear();

            if (samplesPerSegment < 1) {
                samplesPerSegment = 1;
            }

            for (size_t i = 0; i < segments.size(); i++) {

                const Segment& s = segments[i];

                for (int j = 0; j <= samplesPerSegment; j++) {

                    if (i > 0 && j == 0) {
                        continue;
                    }

                    float t = float(j) / float(samplesPerSegment);

                    out.push_back(s.pointAt(t));
                }
            }
        }

        // Boundary tracing
        //--------------------------------------------------

        // The portion of a segment between parameters t0 and t1 (t0 <= t1),
        // preserving kind exactly (arcs stay arcs, so the piece lies exactly on
        // the original curve).
        static Segment subSegment(const Segment& s, float t0, float t1) {
            switch (s.kind) {

                case Segment::Kind::Arc: {
                    float a0 = s.f1 + (s.f2 - s.f1) * t0;
                    float a1 = s.f1 + (s.f2 - s.f1) * t1;
                    return Segment::Arc(s.p0, s.f0, a0, a1);
                }

                case Segment::Kind::Bezier: {
                    Segment left, right;
                    splitSegmentAt(s, t0, left, right);

                    if (t1 >= 1.0f - 1e-6f) { return right; }

                    float u = (t1 - t0) / (1.0f - t0);

                    Segment l2, r2;
                    splitSegmentAt(right, u, l2, r2);

                    return l2;
                }

                default: {
                    return Segment::Line(s.pointAt(t0), s.pointAt(t1));
                }
            }
        }

        // Append the exact path that runs ALONG this chain, forward (increasing
        // segment index, wrapping), from (fromSeg, fromT) to (toSeg, toT).
        // Geometry is preserved, so the traced link lies exactly on the boundary
        // and therefore can never cross it.  "Forward" is the chain's stored
        // direction.
        void traceForwardArc(size_t fromSeg, float fromT, size_t toSeg, float toT, std::vector<Segment>& out) const {

            const size_t n = segments.size();

            if (n == 0) { return; }

            fromSeg %= n;
            toSeg %= n;

            fromT = std::clamp(fromT, 0.0f, 1.0f);
            toT = std::clamp(toT, 0.0f, 1.0f);

            // Short hop within a single segment, no wrap.
            if (fromSeg == toSeg && toT >= fromT) {
                out.push_back(subSegment(segments[fromSeg], fromT, toT));
                return;
            }

            // Tail of the starting segment.
            out.push_back(subSegment(segments[fromSeg], fromT, 1.0f));

            // Whole segments up to (but not including) the target.
            size_t i = (fromSeg + 1) % n;

            for (size_t guard = 0; guard < n; guard++) {

                if (i == toSeg) { break; }

                out.push_back(segments[i]);
                i = (i + 1) % n;
            }

            // Head of the target segment.
            out.push_back(subSegment(segments[toSeg], 0.0f, toT));
        }

        // Arc length of the forward trace from (fromSeg,fromT) to (toSeg,toT).
        float forwardArcLength(size_t fromSeg, float fromT, size_t toSeg, float toT) const {

            std::vector<Segment> tmp;
            traceForwardArc(fromSeg, fromT, toSeg, toT, tmp);

            float length = 0.0f;

            for (const Segment& s : tmp) { length += s.length(); }

            return length;
        }

        // Trace along the chain from (fromSeg,fromT) to (toSeg,toT) the SHORTER
        // way around, emitting exact on-boundary geometry (so it never crosses
        // the boundary).  This is the link used to reach the next raster lane.
        void traceArc(size_t fromSeg, float fromT, size_t toSeg, float toT, std::vector<Segment>& out) const {

            const float forward = forwardArcLength(fromSeg, fromT, toSeg, toT);
            const float backward = forwardArcLength(toSeg, toT, fromSeg, fromT);

            if (forward <= backward) {
                traceForwardArc(fromSeg, fromT, toSeg, toT, out);
                return;
            }

            // Walk the other way: trace forward target -> source, then reverse.
            std::vector<Segment> tmp;
            traceForwardArc(toSeg, toT, fromSeg, fromT, tmp);

            for (size_t i = tmp.size(); i-- > 0; ) {
                out.push_back(tmp[i].reversed());
            }
        }

        // Debug
        //--------------------------------------------------

        void debug(const char* label = "[Chain]") const {
            dbg(
                "%s segs=%zu closed=%i winding=%i area=%.6f cw=%i ccw=%i",
                label,
                size(),
                int(closed()),
                windingSign(),
                signedArea(),
                int(clockwise()),
                int(counterClockwise())
            );
        }
    };
}