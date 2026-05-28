module;

#include <algorithm>
#include <cmath>

export module Rev.Core.Segment3;

import Rev.Core.Pos3;

export namespace Rev::Core {

    struct Segment3 {

        enum class Type { None, Line, Arc, Bezier, Parabola };

        Type kind = Type::None;
        Pos3 a = {}, b = {}, c = {}, d = {};

        // Create
        //--------------------------------------------------

        static Segment3 Line(const Pos3& a, const Pos3& b) { return { Type::Line, a, b }; }
        static Segment3 Arc(const Pos3& a, const Pos3& b, const Pos3& c, const Pos3& d) { return { Type::Arc, a, b, c, d }; }
        static Segment3 Bezier(const Pos3& a, const Pos3& b, const Pos3& c, const Pos3& d) { return { Type::Bezier, a, b, c, d }; }
        static Segment3 Parabola(const Pos3& a, const Pos3& b, const Pos3& c, const Pos3& d) { return { Type::Parabola, a, b, c, d }; }

        // Dispatch
        //--------------------------------------------------

        Pos3 at(float t) const {
            switch (kind) {
                case Type::Line: return lineAt(t);
                case Type::Arc: return arcAt(t);
                case Type::Bezier: return bezierAt(t);
                case Type::Parabola: return parabolaAt(t);
                case Type::None: return Pos3::Invalid();
            }

            return Pos3::Invalid();
        }

        Pos3 tangentAt(float t) const {
            switch (kind) {
                case Type::Line: return lineTangentAt(t);
                case Type::Arc: return arcTangentAt(t);
                case Type::Bezier: return bezierTangentAt(t);
                case Type::Parabola: return parabolaTangentAt(t);
                case Type::None: return Pos3::Invalid();
            }

            return Pos3::Invalid();
        }

        float distanceTo(const Pos3& p) const {
            switch (kind) {
                case Type::Line: return lineDistanceToPoint(p);
                case Type::Arc: return arcDistanceToPoint(p);
                case Type::Bezier: return bezierDistanceToPoint(p);
                case Type::Parabola: return parabolaDistanceToPoint(p);
                case Type::None: return 0.0f;
            }

            return 0.0f;
        }

        float distanceTo(const Segment3& other) const {
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

        bool intersects(const Segment3& other) const {
            switch (kind) {
                case Type::Line: return lineIntersects(other);
                case Type::Arc: return arcIntersects(other);
                case Type::Bezier: return bezierIntersects(other);
                case Type::Parabola: return parabolaIntersects(other);
                case Type::None: return false;
            }

            return false;
        }

        // Helpers
        //--------------------------------------------------

        Pos3 delta() const { return b - a; }
        float chordLength() const { return (b - a).pythag(); }

        // Line
        //--------------------------------------------------

        Pos3 lineAt(float t) const { return a + (b - a) * t; }
        Pos3 lineTangentAt(float t) const { return b - a; }

        float lineDistanceToPoint(const Pos3& p) const {
            Pos3 ab = b - a;
            float len2 = ab.dot(ab);

            if (len2 <= 1e-12f) { return p.distanceTo(a); }

            float t = (p - a).dot(ab) / len2;

            if (t < 0.0f) { t = 0.0f; }
            if (t > 1.0f) { t = 1.0f; }

            return p.distanceTo(lineAt(t));
        }

        bool lineIntersects(const Segment3& other) const {
            switch (other.kind) {
                case Type::Line: return lineIntersectsLine(other);
                case Type::Arc: return lineIntersectsArc(other);
                case Type::Bezier: return lineIntersectsBezier(other);
                case Type::Parabola: return lineIntersectsParabola(other);
                case Type::None: return false;
            }

            return false;
        }

        bool lineIntersectsLine(const Segment3& other) const {
            // 3D line-segment intersection is not generally exact from only
            // coplanar 2D orientation tests. Use closest distance fallback.
            return lineDistanceToLine(other) <= 1e-6f;
        }

        bool lineIntersectsArc(const Segment3& other) const { return sampledIntersects(other); }
        bool lineIntersectsBezier(const Segment3& other) const { return sampledIntersects(other); }
        bool lineIntersectsParabola(const Segment3& other) const { return sampledIntersects(other); }

        float lineDistanceToSegment(const Segment3& other) const {
            switch (other.kind) {
                case Type::Line: return lineDistanceToLine(other);
                case Type::Arc:
                case Type::Bezier:
                case Type::Parabola: return sampledDistanceToSegment(other);
                case Type::None: return 0.0f;
            }

            return 0.0f;
        }

        float lineDistanceToLine(const Segment3& other) const {
            Pos3 u = b - a;
            Pos3 v = other.b - other.a;
            Pos3 w = a - other.a;

            float A = u.dot(u);
            float B = u.dot(v);
            float C = v.dot(v);
            float D = u.dot(w);
            float E = v.dot(w);
            float denom = A * C - B * B;

            float s = 0.0f;
            float t = 0.0f;

            if (denom > 1e-12f) {
                s = (B * E - C * D) / denom;
                t = (A * E - B * D) / denom;
            }

            if (s < 0.0f) { s = 0.0f; }
            if (s > 1.0f) { s = 1.0f; }
            if (t < 0.0f) { t = 0.0f; }
            if (t > 1.0f) { t = 1.0f; }

            return lineAt(s).distanceTo(other.lineAt(t));
        }

        // Arc
        //--------------------------------------------------
        //
        // Placeholder convention:
        // a=start, b=mid/control, c=end, d=auxiliary.
        // Exact arc math can replace these without changing the public shape.

        Pos3 arcAt(float t) const { return lineAt(t); }
        Pos3 arcTangentAt(float t) const { return b - a; }

        float arcDistanceToPoint(const Pos3& p) const { return sampledDistanceToPoint(p); }

        bool arcIntersects(const Segment3& other) const {
            switch (other.kind) {
                case Type::Line: return other.lineIntersectsArc(*this);
                case Type::Arc:
                case Type::Bezier:
                case Type::Parabola: return sampledIntersects(other);
                case Type::None: return false;
            }

            return false;
        }

        float arcDistanceToSegment(const Segment3& other) const { return sampledDistanceToSegment(other); }

        // Bezier
        //--------------------------------------------------

        Pos3 bezierAt(float t) const {
            float u = 1.0f - t;
            return (
                a * (u * u * u) +
                b * (3.0f * u * u * t) +
                c * (3.0f * u * t * t) +
                d * (t * t * t)
            );
        }

        Pos3 bezierTangentAt(float t) const {
            float u = 1.0f - t;
            return (
                (b - a) * (3.0f * u * u) +
                (c - b) * (6.0f * u * t) +
                (d - c) * (3.0f * t * t)
            );
        }

        float bezierDistanceToPoint(const Pos3& p) const { return sampledDistanceToPoint(p); }

        bool bezierIntersects(const Segment3& other) const {
            switch (other.kind) {
                case Type::Line: return other.lineIntersectsBezier(*this);
                case Type::Arc:
                case Type::Bezier:
                case Type::Parabola: return sampledIntersects(other);
                case Type::None: return false;
            }

            return false;
        }

        float bezierDistanceToSegment(const Segment3& other) const { return sampledDistanceToSegment(other); }

        // Parabola
        //--------------------------------------------------

        Pos3 parabolaAt(float t) const {
            float u = 1.0f - t;
            return a * (u * u) + b * (2.0f * u * t) + c * (t * t);
        }

        Pos3 parabolaTangentAt(float t) const {
            return (b - a) * (2.0f * (1.0f - t)) + (c - b) * (2.0f * t);
        }

        float parabolaDistanceToPoint(const Pos3& p) const { return sampledDistanceToPoint(p); }

        bool parabolaIntersects(const Segment3& other) const {
            switch (other.kind) {
                case Type::Line: return other.lineIntersectsParabola(*this);
                case Type::Arc:
                case Type::Bezier:
                case Type::Parabola: return sampledIntersects(other);
                case Type::None: return false;
            }

            return false;
        }

        float parabolaDistanceToSegment(const Segment3& other) const { return sampledDistanceToSegment(other); }

        // Sampled fallbacks
        //--------------------------------------------------

        float sampledDistanceToPoint(const Pos3& p, int samples = 24) const {
            float best = p.distanceTo(at(0.0f));

            for (int i = 1; i <= samples; i++) {
                float d = p.distanceTo(at(float(i) / float(samples)));
                if (d < best) { best = d; }
            }

            return best;
        }

        float sampledDistanceToSegment(const Segment3& other, int samples = 24) const {
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

        bool sampledIntersects(const Segment3& other, int samples = 24) const {
            for (int i = 0; i < samples; i++) {
                Segment3 aLine = Line(at(float(i) / float(samples)), at(float(i + 1) / float(samples)));

                for (int j = 0; j < samples; j++) {
                    Segment3 bLine = Line(other.at(float(j) / float(samples)), other.at(float(j + 1) / float(samples)));
                    if (aLine.lineIntersectsLine(bLine)) { return true; }
                }
            }

            return false;
        }
    };
}