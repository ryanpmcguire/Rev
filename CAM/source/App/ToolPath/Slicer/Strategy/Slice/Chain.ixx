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

        Chain() {}

        Chain(const std::vector<Segment>& source) {
            build(source);
        }

        static Chain From(const std::vector<Segment>& source) {
            return Chain(source);
        }

        static std::vector<Chain> BuildAll(
            const std::vector<Segment>& source,
            float eps = 1e-4f
        ) {
            std::vector<Segment> remaining = source;
            std::vector<Chain> out;

            while (!remaining.empty()) {

                Chain chain;

                chain.buildOneFromRemaining(
                    remaining,
                    eps
                );

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

        void build(
            const std::vector<Segment>& source,
            float eps = 1e-4f
        ) {
            clear();

            std::vector<Segment> remaining = source;

            buildOneFromRemaining(
                remaining,
                eps
            );
        }

        void buildOneFromRemaining(
            std::vector<Segment>& remaining,
            float eps = 1e-4f
        ) {
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

                    float dStart = current.distanceTo(
                        remaining[i].start()
                    );

                    float dEnd = current.distanceTo(
                        remaining[i].end()
                    );

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

                Segment next = (
                    reverseSegment
                    ? remaining[best].reversed()
                    : remaining[best]
                );

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

            return std::abs(
                (mx.x - mn.x) *
                (mx.y - mn.y)
            );
        }

        // Winding
        //--------------------------------------------------

        float signedArea(
            int samplesPerSegment = 8
        ) const {
            if (segments.empty()) { return 0.0f; }

            std::vector<Pos> pts;

            sample(
                pts,
                samplesPerSegment
            );

            if (pts.size() < 3) { return 0.0f; }

            float area = 0.0f;

            for (size_t i = 0; i < pts.size(); i++) {

                const Pos& a = pts[i];
                const Pos& b = pts[(i + 1) % pts.size()];

                area += (
                    a.x * b.y -
                    b.x * a.y
                );
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
                out.push_back(
                    segments[i - 1].reversed()
                );
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

        Pos leftNormalAt(
            size_t segmentIndex,
            float t = 0.5f
        ) const {
            if (segmentIndex >= segments.size()) {
                return { 0.0f, 0.0f };
            }

            Pos tangent = segments[segmentIndex].tangentAt(t);

            return {
                -tangent.y,
                tangent.x
            };
        }

        Pos rightNormalAt(
            size_t segmentIndex,
            float t = 0.5f
        ) const {
            return leftNormalAt(segmentIndex, t) * -1.0f;
        }

        Pos interiorNormalAt(
            size_t segmentIndex,
            float t = 0.5f
        ) const {
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

        Pos exteriorNormalAt(
            size_t segmentIndex,
            float t = 0.5f
        ) const {
            return interiorNormalAt(segmentIndex, t) * -1.0f;
        }

        // Offsetting
        //--------------------------------------------------

        Segment translatedSegment(
            const Segment& s,
            const Pos& offset
        ) const {
            switch (s.kind) {

                case Segment::Kind::Line: {
                    return Segment::Line(
                        s.p0 + offset,
                        s.p1 + offset
                    );
                }

                case Segment::Kind::Arc: {
                    return Segment::Arc(
                        s.p0 + offset,
                        s.f0,
                        s.f1,
                        s.f2
                    );
                }

                case Segment::Kind::Bezier: {
                    return Segment::Bezier(
                        s.p0 + offset,
                        s.p1 + offset,
                        s.p2 + offset,
                        s.p3 + offset
                    );
                }

                default: {
                    return s;
                }
            }
        }

        Segment offsetSegmentNormal(
            const Segment& s,
            float amount,
            bool towardInterior
        ) const {
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
                        return Segment::Line(
                            s.p0,
                            s.p0
                        );
                    }

                    return Segment::Arc(
                        s.p0,
                        radius,
                        s.f1,
                        s.f2
                    );
                }

                default: {
                    Pos n = towardInterior
                        ? interiorNormalAtSegment(s)
                        : exteriorNormalAtSegment(s);

                    return translatedSegment(
                        s,
                        n * amount
                    );
                }
            }
        }

        Pos interiorNormalAtSegment(
            const Segment& s,
            float t = 0.5f
        ) const {
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

        Pos exteriorNormalAtSegment(
            const Segment& s,
            float t = 0.5f
        ) const {
            return interiorNormalAtSegment(s, t) * -1.0f;
        }

        Chain offsetInterior(float amount) const {

            Chain out;

            if (empty()) { return out; }

            for (size_t i = 0; i < segments.size(); i++) {

                out.push(
                    offsetSegmentNormal(
                        segments[i],
                        amount,
                        true
                    )
                );
            }

            out.relinkNeighbors();

            return out;
        }

        Chain offsetExterior(float amount) const {

            Chain out;

            if (empty()) { return out; }

            for (size_t i = 0; i < segments.size(); i++) {

                out.push(
                    offsetSegmentNormal(
                        segments[i],
                        amount,
                        false
                    )
                );
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

        bool alreadyLinked(
            size_t i,
            size_t j,
            float eps = 1e-4f
        ) const {
            return segments[i].end().distanceTo(
                segments[j].start()
            ) <= eps;
        }

        void relinkNeighbors(float eps = 1e-4f) {

            if (segments.size() < 2) { return; }

            for (size_t i = 0; i < segments.size(); i++) {

                size_t j = (i + 1) % segments.size();

                if (alreadyLinked(i, j, eps)) {
                    continue;
                }

                Pos p = segments[i].generalizedIntersection(
                    segments[j],
                    eps
                );

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

                    Pos p = segments[i].intersection(
                        segments[j]
                    );

                    if (!p) { continue; }

                    segments[i] = Segment::Line(p, p);
                    segments[j] = Segment::Line(p, p);
                }
            }
        }

        // Sampling
        //--------------------------------------------------

        void sample(
            std::vector<Pos>& out,
            int samplesPerSegment = 8
        ) const {
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

                    out.push_back(
                        s.pointAt(t)
                    );
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