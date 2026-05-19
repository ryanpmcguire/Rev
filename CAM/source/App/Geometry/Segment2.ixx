module;

#include <algorithm>
#include <cmath>

export module Rev.Core.Segment;

import Rev.Core.Pos;

export namespace Rev::Core {

    struct Segment {

        enum class Type { None, Line, Arc, Bezier, Parabola };

        Type kind = Type::None;
        Pos a = {}, b = {}, c = {}, d = {};

        static Segment Line(const Pos& a, const Pos& b) { return { Type::Line, a, b }; }
        static Segment Arc(const Pos& a, const Pos& b, const Pos& c, const Pos& d) { return { Type::Arc, a, b, c, d }; }
        static Segment Bezier(const Pos& a, const Pos& b, const Pos& c, const Pos& d) { return { Type::Bezier, a, b, c, d }; }
        static Segment Parabola(const Pos& a, const Pos& b, const Pos& c, const Pos& d) { return { Type::Parabola, a, b, c, d }; }

        Pos at(float t) const {
            switch (kind) {
                case Type::Line: return lineAt(t);
                case Type::Arc: return arcAt(t);
                case Type::Bezier: return bezierAt(t);
                case Type::Parabola: return parabolaAt(t);
                case Type::None: return Pos::Invalid();
            }

            return Pos::Invalid();
        }

        Pos tangentAt(float t) const {
            switch (kind) {
                case Type::Line: return lineTangentAt(t);
                case Type::Arc: return arcTangentAt(t);
                case Type::Bezier: return bezierTangentAt(t);
                case Type::Parabola: return parabolaTangentAt(t);
                case Type::None: return Pos::Invalid();
            }

            return Pos::Invalid();
        }

        float distanceTo(const Pos& p) const {
            switch (kind) {
                case Type::Line: return lineDistanceToPoint(p);
                case Type::Arc: return arcDistanceToPoint(p);
                case Type::Bezier: return bezierDistanceToPoint(p);
                case Type::Parabola: return parabolaDistanceToPoint(p);
                case Type::None: return 0.0f;
            }

            return 0.0f;
        }

        float distanceTo(const Segment& other) const {
            if (intersects(other)) { return 0.0f; }

            switch (kind) {
                case Type::Line: return lineDistanceToSegment(other);
                case Type::Arc: return arcDistanceToSegment(other);
                case Type::Bezier: return bezierDistanceToSegment(other);
                case Type::Parabola: return parabolaDistanceToSegment(other);
                case Type::None: return 0.0f;
            }

            return 0.0f;
        }

        bool intersects(const Segment& other) const {
            switch (kind) {
                case Type::Line: return lineIntersects(other);
                case Type::Arc: return arcIntersects(other);
                case Type::Bezier: return bezierIntersects(other);
                case Type::Parabola: return parabolaIntersects(other);
                case Type::None: return false;
            }

            return false;
        }

        Pos delta() const { return b - a; }
        float chordLength() const { return (b - a).pythag(); }

        // Line
        //--------------------------------------------------

        Pos lineAt(float t) const { return a + (b - a) * t; }
        Pos lineTangentAt(float t) const { return b - a; }

        float lineDistanceToPoint(const Pos& p) const {
            Pos ab = b - a;
            float len2 = ab.dot(ab);

            if (len2 <= 1e-12f) { return p.distanceTo(a); }

            float t = (p - a).dot(ab) / len2;

            if (t < 0.0f) { t = 0.0f; }
            if (t > 1.0f) { t = 1.0f; }

            return p.distanceTo(lineAt(t));
        }

        bool lineIntersects(const Segment& other) const {
            switch (other.kind) {
                case Type::Line: return lineIntersectsLine(other);
                case Type::Arc: return lineIntersectsArc(other);
                case Type::Bezier: return lineIntersectsBezier(other);
                case Type::Parabola: return lineIntersectsParabola(other);
                case Type::None: return false;
            }

            return false;
        }

        bool lineIntersectsLine(const Segment& other) const {
            Pos r = b - a;
            Pos s = other.b - other.a;
            float denom = r.cross(s);

            if (std::abs(denom) < 1e-6f) { return false; }

            Pos qp = other.a - a;
            float t = qp.cross(s) / denom;
            float u = qp.cross(r) / denom;

            return t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f;
        }

        bool lineIntersectsArc(const Segment& other) const { return sampledIntersects(other); }
        bool lineIntersectsBezier(const Segment& other) const { return sampledIntersects(other); }
        bool lineIntersectsParabola(const Segment& other) const { return sampledIntersects(other); }

        float lineDistanceToSegment(const Segment& other) const {
            switch (other.kind) {
                case Type::Line: return lineDistanceToLine(other);
                case Type::Arc:
                case Type::Bezier:
                case Type::Parabola: return sampledDistanceToSegment(other);
                case Type::None: return 0.0f;
            }

            return 0.0f;
        }

        float lineDistanceToLine(const Segment& other) const {
            if (lineIntersectsLine(other)) { return 0.0f; }

            float d0 = other.lineDistanceToPoint(a);
            float d1 = other.lineDistanceToPoint(b);
            float d2 = lineDistanceToPoint(other.a);
            float d3 = lineDistanceToPoint(other.b);

            return std::min(std::min(d0, d1), std::min(d2, d3));
        }

        // Arc
        //--------------------------------------------------
        //
        // Placeholder convention:
        // a=start, b=mid/control, c=end, d=auxiliary.
        // Exact arc math can replace these without changing the public shape.

        static float twoPi() { return 6.2831853071795864769f; }

        static float positiveAngleDelta(float from, float to) {
            float d = to - from;
            while (d < 0.0f) { d += twoPi(); }
            while (d >= twoPi()) { d -= twoPi(); }
            return d;
        }

        float arcStartAngle() const { return std::atan2(a.y - d.y, a.x - d.x); }
        float arcMidAngle() const { return std::atan2(b.y - d.y, b.x - d.x); }
        float arcEndAngle() const { return std::atan2(c.y - d.y, c.x - d.x); }

        bool arcIsCcw() const {
            float a0 = arcStartAngle();
            float am = arcMidAngle();
            float a1 = arcEndAngle();

            float sweep = positiveAngleDelta(a0, a1);
            float mid = positiveAngleDelta(a0, am);

            return mid <= sweep + 1e-5f;
        }

        float arcSweep() const {
            float a0 = arcStartAngle();
            float a1 = arcEndAngle();

            if (arcIsCcw()) { return positiveAngleDelta(a0, a1); }
            return -positiveAngleDelta(a1, a0);
        }

        Pos arcAt(float t) const {
            float r = a.distanceTo(d);
            float angle = arcStartAngle() + arcSweep() * t;

            return {
                d.x + std::cos(angle) * r,
                d.y + std::sin(angle) * r
            };
        }

        Pos arcTangentAt(float t) const {
            float r = a.distanceTo(d);
            float sweep = arcSweep();
            float angle = arcStartAngle() + sweep * t;

            return {
                -std::sin(angle) * r * sweep,
                std::cos(angle) * r * sweep
            };
        }

        float arcDistanceToPoint(const Pos& p) const { return sampledDistanceToPoint(p); }

        bool arcIntersects(const Segment& other) const {
            switch (other.kind) {
                case Type::Line: return other.lineIntersectsArc(*this);
                case Type::Arc:
                case Type::Bezier:
                case Type::Parabola: return sampledIntersects(other);
                case Type::None: return false;
            }

            return false;
        }

        float arcDistanceToSegment(const Segment& other) const { return sampledDistanceToSegment(other); }

        // Bezier
        //--------------------------------------------------

        Pos bezierAt(float t) const {
            float u = 1.0f - t;
            return (
                a * (u * u * u) +
                b * (3.0f * u * u * t) +
                c * (3.0f * u * t * t) +
                d * (t * t * t)
            );
        }

        Pos bezierTangentAt(float t) const {
            float u = 1.0f - t;
            return (
                (b - a) * (3.0f * u * u) +
                (c - b) * (6.0f * u * t) +
                (d - c) * (3.0f * t * t)
            );
        }

        float bezierDistanceToPoint(const Pos& p) const { return sampledDistanceToPoint(p); }

        bool bezierIntersects(const Segment& other) const {
            switch (other.kind) {
                case Type::Line: return other.lineIntersectsBezier(*this);
                case Type::Arc:
                case Type::Bezier:
                case Type::Parabola: return sampledIntersects(other);
                case Type::None: return false;
            }

            return false;
        }

        float bezierDistanceToSegment(const Segment& other) const { return sampledDistanceToSegment(other); }

        // Parabola
        //--------------------------------------------------

        Pos parabolaAt(float t) const {
            float u = 1.0f - t;
            return a * (u * u) + b * (2.0f * u * t) + c * (t * t);
        }

        Pos parabolaTangentAt(float t) const {
            return (b - a) * (2.0f * (1.0f - t)) + (c - b) * (2.0f * t);
        }

        float parabolaDistanceToPoint(const Pos& p) const { return sampledDistanceToPoint(p); }

        bool parabolaIntersects(const Segment& other) const {
            switch (other.kind) {
                case Type::Line: return other.lineIntersectsParabola(*this);
                case Type::Arc:
                case Type::Bezier:
                case Type::Parabola: return sampledIntersects(other);
                case Type::None: return false;
            }

            return false;
        }

        float parabolaDistanceToSegment(const Segment& other) const { return sampledDistanceToSegment(other); }

        // Sampled fallbacks
        //--------------------------------------------------

        float sampledDistanceToPoint(const Pos& p, int samples = 24) const {
            float best = p.distanceTo(at(0.0f));

            for (int i = 1; i <= samples; i++) {
                float d = p.distanceTo(at(float(i) / float(samples)));
                if (d < best) { best = d; }
            }

            return best;
        }

        float sampledDistanceToSegment(const Segment& other, int samples = 24) const {
            float best = distanceTo(other.at(0.0f));

            for (int i = 0; i <= samples; i++) {
                float d = other.distanceTo(at(float(i) / float(samples)));
                if (d < best) { best = d; }
            }

            for (int i = 0; i <= samples; i++) {
                float d = distanceTo(other.at(float(i) / float(samples)));
                if (d < best) { best = d; }
            }

            return best;
        }

        bool sampledIntersects(const Segment& other, int samples = 24) const {
            for (int i = 0; i < samples; i++) {
                Segment aLine = Line(at(float(i) / float(samples)), at(float(i + 1) / float(samples)));

                for (int j = 0; j < samples; j++) {
                    Segment bLine = Line(other.at(float(j) / float(samples)), other.at(float(j + 1) / float(samples)));
                    if (aLine.lineIntersectsLine(bLine)) { return true; }
                }
            }

            return false;
        }
    };
}