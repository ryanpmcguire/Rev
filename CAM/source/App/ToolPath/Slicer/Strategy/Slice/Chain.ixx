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

        Chain offsetInterior(float amount) const {

            Chain out;

            if (empty()) { return out; }

            for (size_t i = 0; i < segments.size(); i++) {

                out.push(offsetSegmentNormal(segments[i], amount, true));
            }

            out.relinkNeighbors();

            return out;
        }

        Chain offsetExterior(float amount) const {

            Chain out;

            if (empty()) { return out; }

            for (size_t i = 0; i < segments.size(); i++) {

                out.push(offsetSegmentNormal(segments[i], amount, false));
            }

            out.relinkNeighbors();

            return out;
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

        bool alreadyLinked(size_t i, size_t j, float eps = 1e-4f) const {
            return segments[i].end().distanceTo(segments[j].start()) <= eps;
        }

        void relinkNeighbors(float eps = 1e-4f) {

            if (segments.size() < 2) { return; }

            for (size_t i = 0; i < segments.size(); i++) {

                size_t j = (i + 1) % segments.size();

                if (alreadyLinked(i, j, eps)) {
                    continue;
                }

                Pos p = segments[i].generalizedIntersection(segments[j], eps);

                if (!p) { continue; }

                segments[i].setEnd(p);
                segments[j].setStart(p);
            }
        }

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

        // Proper crossing of (a,b) and (c,d): returns the crossing point, but
        // only for a real interior crossing (strict 0<t<1, 0<u<1), so shared
        // endpoints / collinear touches at legitimate joins are excluded.
        static bool segmentIntersectionPoint(
            const Pos& a, const Pos& b,
            const Pos& c, const Pos& d,
            Pos& out
        ) {
            const float rx = b.x - a.x, ry = b.y - a.y;
            const float sx = d.x - c.x, sy = d.y - c.y;

            const float denom = rx * sy - ry * sx;

            if (std::abs(denom) < 1e-12f) { return false; }  // parallel / collinear

            const float t = ((c.x - a.x) * sy - (c.y - a.y) * sx) / denom;
            const float u = ((c.x - a.x) * ry - (c.y - a.y) * rx) / denom;

            if (t <= 0.0f || t >= 1.0f || u <= 0.0f || u >= 1.0f) { return false; }

            out = { a.x + t * rx, a.y + t * ry };

            return true;
        }

        // Fractalize: split a (possibly self-intersecting) chain into its simple
        // closed sub-loops.  At each self-intersection the loop is pinched into
        // two: the part between the two crossing edges, and the remainder; both
        // close at the crossing point.  We recurse until every piece is simple.
        //
        // A non-self-intersecting chain is returned unchanged (exact geometry,
        // arcs preserved); only tangled offsets are linearised via sampling.
        // Orientation/area filtering (dropping inverted pinch lobes) is left to
        // the caller, which knows the source winding.
        std::vector<Chain> splitSimpleLoops(float eps = 1e-4f) const {

            if (!selfIntersects(eps)) {
                return { *this };
            }

            std::vector<Pos> base;
            sample(base, 8);

            if (
                base.size() >= 2 &&
                base.front().distanceTo(base.back()) <= eps
            ) {
                base.pop_back();
            }

            std::vector<Chain> out;
            std::vector<std::vector<Pos>> work;
            work.push_back(base);

            int guard = 0;
            const int guardMax = 8192;

            while (!work.empty() && guard++ < guardMax) {

                std::vector<Pos> poly = work.back();
                work.pop_back();

                const size_t m = poly.size();

                if (m < 3) { continue; }

                bool found = false;
                size_t fi = 0, fj = 0;
                Pos p;

                for (size_t i = 0; i < m && !found; i++) {

                    const Pos& a0 = poly[i];
                    const Pos& a1 = poly[(i + 1) % m];

                    for (size_t j = i + 2; j < m; j++) {

                        if (i == 0 && j == m - 1) { continue; }  // wrap-adjacent

                        const Pos& b0 = poly[j];
                        const Pos& b1 = poly[(j + 1) % m];

                        if (segmentIntersectionPoint(a0, a1, b0, b1, p)) {
                            found = true;
                            fi = i;
                            fj = j;
                            break;
                        }
                    }
                }

                if (!found) {

                    Chain loop;

                    for (size_t k = 0; k < m; k++) {
                        loop.push(Segment::Line(poly[k], poly[(k + 1) % m]));
                    }

                    out.push_back(loop);
                    continue;
                }

                // Inner loop: crossing point + poly[fi+1 .. fj].
                std::vector<Pos> loopA;
                loopA.push_back(p);
                for (size_t k = fi + 1; k <= fj; k++) { loopA.push_back(poly[k]); }

                // Outer remainder: crossing point + poly[fj+1 ..] + poly[.. fi].
                std::vector<Pos> loopB;
                loopB.push_back(p);
                for (size_t k = fj + 1; k < m; k++) { loopB.push_back(poly[k]); }
                for (size_t k = 0; k <= fi; k++) { loopB.push_back(poly[k]); }

                work.push_back(loopA);
                work.push_back(loopB);
            }

            return out;
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