module;

#include <vector>
#include <algorithm>
#include <cmath>

export module Cam.App.Slicer.Strategy.Slice.Segment2;

import Rev.Core.Pos;

export namespace Cam::App::Slicer::Strategy::Slice {

    using namespace Rev::Core;

    struct Segment {

        enum class Kind {
            None,
            Line,
            Arc,
            Bezier
        };

        struct YHit {
            Pos point = { 0.0f, 0.0f };
            float t = 0.0f;
        };

        Kind kind = Kind::None;

        // Unified fixed storage.
        //
        // Line:
        //     p0 = start
        //     p1 = end
        //
        // Arc:
        //     p0 = center
        //     f0 = radius
        //     f1 = start angle
        //     f2 = end angle
        //
        // Bezier:
        //     p0, p1, p2, p3 = control points
        Pos p0 = { 0.0f, 0.0f };
        Pos p1 = { 0.0f, 0.0f };
        Pos p2 = { 0.0f, 0.0f };
        Pos p3 = { 0.0f, 0.0f };

        float f0 = 0.0f;
        float f1 = 0.0f;
        float f2 = 0.0f;
        float f3 = 0.0f;

        // Create
        //--------------------------------------------------

        static Segment Line(const Pos& a, const Pos& b) {
            Segment s;

            s.kind = Kind::Line;
            s.p0 = a;
            s.p1 = b;

            return s;
        }

        static Segment Arc(const Pos& center, float radius, float a0, float a1) {
            Segment s;

            s.kind = Kind::Arc;
            s.p0 = center;
            s.f0 = radius;
            s.f1 = a0;
            s.f2 = a1;

            return s;
        }

        static Segment Bezier(const Pos& p0, const Pos& p1, const Pos& p2, const Pos& p3) {
            Segment s;

            s.kind = Kind::Bezier;
            s.p0 = p0;
            s.p1 = p1;
            s.p2 = p2;
            s.p3 = p3;

            return s;
        }

        // Endpoints
        //--------------------------------------------------

        Pos start() const {

            switch (kind) {

                case Kind::Line: {
                    return p0;
                }

                case Kind::Arc: {
                    return {
                        p0.x + std::cos(f1) * f0,
                        p0.y + std::sin(f1) * f0
                    };
                }

                case Kind::Bezier: {
                    return p0;
                }

                default: {
                    return Pos::Invalid();
                }
            }
        }

        Pos end() const {

            switch (kind) {

                case Kind::Line: {
                    return p1;
                }

                case Kind::Arc: {
                    return {
                        p0.x + std::cos(f2) * f0,
                        p0.y + std::sin(f2) * f0
                    };
                }

                case Kind::Bezier: {
                    return p3;
                }

                default: {
                    return Pos::Invalid();
                }
            }
        }

        void setStart(const Pos& p) {
            switch (kind) {

                case Kind::Line: {
                    p0 = p;
                    break;
                }

                case Kind::Bezier: {
                    p0 = p;
                    break;
                }

                default: {
                    break;
                }
            }
        }

        void setEnd(const Pos& p) {
            switch (kind) {

                case Kind::Line: {
                    p1 = p;
                    break;
                }

                case Kind::Bezier: {
                    p3 = p;
                    break;
                }

                default: {
                    break;
                }
            }
        }

        Segment reversed() const {

            switch (kind) {

                case Kind::Line: {
                    return Segment::Line(p1, p0);
                }

                case Kind::Arc: {
                    return Segment::Arc(p0, f0, f2, f1);
                }

                case Kind::Bezier: {
                    return Segment::Bezier(p3, p2, p1, p0);
                }

                default: {
                    return Segment();
                }
            }
        }

        // Evaluation
        //--------------------------------------------------

        Pos at(float t) const { return pointAt(t); }
        Pos pointAt(float t) const {
            t = std::clamp(t, 0.0f, 1.0f);

            switch (kind) {

                case Kind::Line: {
                    return p0 + (p1 - p0) * t;
                }

                case Kind::Arc: {

                    float a = f1 + (f2 - f1) * t;

                    return {
                        p0.x + std::cos(a) * f0,
                        p0.y + std::sin(a) * f0
                    };
                }

                case Kind::Bezier: {

                    float u = 1.0f - t;

                    return (
                        p0 * (u * u * u) +
                        p1 * (3.0f * u * u * t) +
                        p2 * (3.0f * u * t * t) +
                        p3 * (t * t * t)
                    );
                }

                default: {
                    return Pos::Invalid();
                }
            }
        }

        Pos tangentAt(float t) const {
            t = std::clamp(t, 0.0f, 1.0f);

            Pos d = { 1.0f, 0.0f };

            switch (kind) {

                case Kind::Line: {
                    d = p1 - p0;
                    break;
                }

                case Kind::Arc: {

                    float a = f1 + (f2 - f1) * t;
                    float sign = (f2 >= f1) ? 1.0f : -1.0f;

                    d = {
                        -std::sin(a) * sign,
                        std::cos(a) * sign
                    };

                    break;
                }

                case Kind::Bezier: {

                    float u = 1.0f - t;

                    d = (
                        (p1 - p0) * (3.0f * u * u) +
                        (p2 - p1) * (6.0f * u * t) +
                        (p3 - p2) * (3.0f * t * t)
                    );

                    break;
                }

                default: {
                    break;
                }
            }

            float len = d.pythag();

            if (len <= 1e-8f) {
                return { 1.0f, 0.0f };
            }

            return d * (1.0f / len);
        }

        Pos startTangency() const {
            return tangentAt(0.0f);
        }

        Pos endTangency() const {
            return tangentAt(1.0f);
        }

        Pos normalAt(float t) const {
            Pos tangent = tangentAt(t);

            return {
                -tangent.y,
                tangent.x
            };
        }

        bool arcIsCcw() const {
            if (kind != Kind::Arc) { return false; }
            return f2 >= f1;
        }

        // Bounds / measures
        //--------------------------------------------------

        Pos min(int samples = 32) const {
            if (kind == Kind::Line) {
                return Pos::min(p0, p1);
            }

            Pos out = pointAt(0.0f);

            if (samples < 1) { samples = 1; }

            for (int i = 1; i <= samples; i++) {
                out = Pos::min(out, pointAt(float(i) / float(samples)));
            }

            return out;
        }

        Pos max(int samples = 32) const {
            if (kind == Kind::Line) {
                return Pos::max(p0, p1);
            }

            Pos out = pointAt(0.0f);

            if (samples < 1) { samples = 1; }

            for (int i = 1; i <= samples; i++) {
                out = Pos::max(out, pointAt(float(i) / float(samples)));
            }

            return out;
        }

        float length(int samples = 32) const {
            if (kind == Kind::Line) {
                return (p1 - p0).pythag();
            }

            if (samples < 1) { samples = 1; }

            float total = 0.0f;
            Pos prev = pointAt(0.0f);

            for (int i = 1; i <= samples; i++) {

                Pos p = pointAt(float(i) / float(samples));

                total += (p - prev).pythag();
                prev = p;
            }

            return total;
        }

        bool valid() const {
            return length() > 1e-6f;
        }

        // Distances
        //--------------------------------------------------

        static float pointLineDistance(const Pos& p, const Pos& a, const Pos& b) {
            Pos ab = b - a;

            float len2 = ab.dot(ab);

            if (len2 <= 1e-12f) {
                return p.distanceTo(a);
            }

            float t = (p - a).dot(ab) / len2;
            t = std::clamp(t, 0.0f, 1.0f);

            Pos q = a + ab * t;

            return p.distanceTo(q);
        }

        static float orient(const Pos& a, const Pos& b, const Pos& c) {
            return (b - a).cross(c - a);
        }

        static bool rangesOverlap(float a0, float a1, float b0, float b1) {
            if (a0 > a1) { std::swap(a0, a1); }
            if (b0 > b1) { std::swap(b0, b1); }

            return std::max(a0, b0) <= std::min(a1, b1);
        }

        static bool lineLineSegmentsIntersect(const Pos& a0, const Pos& a1, const Pos& b0, const Pos& b1, float eps = 1e-6f) {
            float o1 = orient(a0, a1, b0);
            float o2 = orient(a0, a1, b1);
            float o3 = orient(b0, b1, a0);
            float o4 = orient(b0, b1, a1);

            if (
                ((o1 > eps && o2 < -eps) || (o1 < -eps && o2 > eps)) &&
                ((o3 > eps && o4 < -eps) || (o3 < -eps && o4 > eps))
            ) {
                return true;
            }

            if (
                std::abs(o1) <= eps ||
                std::abs(o2) <= eps ||
                std::abs(o3) <= eps ||
                std::abs(o4) <= eps
            ) {
                return (
                    rangesOverlap(a0.x, a1.x, b0.x, b1.x) &&
                    rangesOverlap(a0.y, a1.y, b0.y, b1.y)
                );
            }

            return false;
        }

        static float lineLineDistance(const Pos& a0, const Pos& a1, const Pos& b0, const Pos& b1) {
            if (lineLineSegmentsIntersect(a0, a1, b0, b1)) {
                return 0.0f;
            }

            float d0 = pointLineDistance(a0, b0, b1);
            float d1 = pointLineDistance(a1, b0, b1);
            float d2 = pointLineDistance(b0, a0, a1);
            float d3 = pointLineDistance(b1, a0, a1);

            return std::min(std::min(d0, d1), std::min(d2, d3));
        }

        float distanceTo(const Pos& p, int samples = 64) const {
            if (kind == Kind::Line) { return pointLineDistance(p, p0, p1); }

            float best = 1e30f;

            if (samples < 1) { samples = 1; }

            for (int i = 0; i <= samples; i++) {

                Pos q = pointAt(float(i) / float(samples));

                best = std::min(best, p.distanceTo(q));
            }

            return best;
        }

        float distanceTo(const Segment& other, int samples = 32) const {
            if (kind == Kind::Line && other.kind == Kind::Line) {
                return lineLineDistance(p0, p1, other.p0, other.p1);
            }

            float best = 1e30f;

            if (samples < 1) { samples = 1; }

            for (int i = 0; i <= samples; i++) {

                Pos p = pointAt(float(i) / float(samples));

                best = std::min(best, other.distanceTo(p, samples));
            }

            for (int i = 0; i <= samples; i++) {

                Pos p = other.pointAt(float(i) / float(samples));

                best = std::min(best, distanceTo(p, samples));
            }

            return best;
        }

        // Intersections
        //--------------------------------------------------

        bool intersection(const Segment& other, Pos& out, float eps = 1e-6f) const {
            if (kind == Kind::Line && other.kind == Kind::Line) {
                return lineLineIntersection(*this, other, out, eps);
            }

            return false;
        }

        Pos intersection(const Segment& other, float eps = 1e-6f) const {
            Pos out;
            if (!intersection(other, out, eps)) { return Pos::Invalid(); }

            return out;
        }

        Pos generalizedIntersection(const Segment& other, float eps = 1e-6f) const {
            Pos p = intersection(other, eps);

            if (p) {
                return p;
            }

            // Fallback used for relinking nearly adjacent segments.
            Pos a = end();
            Pos b = other.start();

            if (a.distanceTo(b) <= eps) {
                return (a + b) * 0.5f;
            }

            return Pos::Invalid();
        }

        static bool lineLineIntersection(const Segment& a, const Segment& b, Pos& out, float eps = 1e-6f) {
            if (a.kind != Kind::Line || b.kind != Kind::Line) { return false; }

            Pos p = a.p0;
            Pos r = a.p1 - a.p0;

            Pos q = b.p0;
            Pos s = b.p1 - b.p0;

            float rxs = r.cross(s);

            if (std::abs(rxs) <= eps) {
                return false;
            }

            float t = (q - p).cross(s) / rxs;
            float u = (q - p).cross(r) / rxs;

            if (
                t < -eps ||
                t > 1.0f + eps ||
                u < -eps ||
                u > 1.0f + eps
            ) {
                return false;
            }

            t = std::clamp(t, 0.0f, 1.0f);

            out = p + r * t;

            return true;
        }

        // Scanline hits
        //--------------------------------------------------

        void hitsAtY(float y, std::vector<YHit>& out) const {
            switch (kind) {

                case Kind::Line: {
                    lineHitsAtY(y, out);
                    break;
                }

                default: {
                    sampledHitsAtY(y, out);
                    break;
                }
            }
        }

        void lineHitsAtY(float y, std::vector<YHit>& out) const {
            if (kind != Kind::Line) { return; }

            if (std::abs(p0.y - p1.y) <= 1e-6f) {
                return;
            }

            float yMin = std::min(p0.y, p1.y);
            float yMax = std::max(p0.y, p1.y);

            // Half-open interval avoids double-counting vertices.
            if (y < yMin || y >= yMax) {
                return;
            }

            float t = (y - p0.y) / (p1.y - p0.y);
            Pos p = pointAt(t);

            out.push_back({
                .point = p,
                .t = t
            });
        }

        void sampledHitsAtY(float y, std::vector<YHit>& out, int samples = 64) const {
            if (samples < 1) { samples = 1; }

            Pos prev = pointAt(0.0f);

            for (int i = 1; i <= samples; i++) {

                float t1 = float(i) / float(samples);
                Pos next = pointAt(t1);

                if (std::abs(prev.y - next.y) <= 1e-6f) {
                    prev = next;
                    continue;
                }

                float yMin = std::min(prev.y, next.y);
                float yMax = std::max(prev.y, next.y);

                if (y < yMin || y >= yMax) {
                    prev = next;
                    continue;
                }

                float local = (y - prev.y) / (next.y - prev.y);
                Pos p = prev + (next - prev) * local;

                float t0 = float(i - 1) / float(samples);
                float t = t0 + (t1 - t0) * local;

                out.push_back({
                    .point = p,
                    .t = t
                });

                prev = next;
            }
        }

        // Sampling
        //--------------------------------------------------

        void sample(std::vector<Pos>& out, int samples = 16) const {
            if (samples < 1) { samples = 1; }
            for (int i = 0; i <= samples; i++) { out.push_back(pointAt(float(i) / float(samples))); }
        }
    };
}