module;

#include <cmath>
#include <vector>

export module Sketch.App.Tool;

import Rev.Core.Pos;

import Sketch.App.Geometry;
import Sketch.App.Project;

export namespace Sketch::App {

    using Rev::Core::Pos;

    inline constexpr float PI  = 3.14159265358979323846f;
    inline constexpr float TAU = 2.0f * PI;

    // Preview geometry, split by the role each piece plays. The view renders
    // each bucket in its own colour:
    //
    //   real         - the actual committed geometry (not held here)
    //   construction - construction-only helpers (reserved; unused for now)
    //   candidate    - *intent*: what is about to become real geometry
    //   helper       - transient display geometry (radius lines, ghost circles)
    struct SketchPreview {
        SketchGeometry candidate;
        SketchGeometry construction;
        SketchGeometry helper;
    };

    // A sketch tool *is* a little state machine. Each tool owns its own pending
    // state and decides what a click / enter / escape means, so the view stays a
    // dumb input router instead of a tangle of per-tool branches.
    struct Tool {

        virtual ~Tool() = default;

        // A left click landed at world position w.
        virtual void click(Project& project, Pos w) = 0;

        // The cursor moved to world position w while this tool is active. Tools
        // that need to integrate motion (e.g. arc direction) track it here.
        virtual void hover(Pos w) {}

        // Enter pressed: finish/restart the current run.
        virtual void enter(Project& project) {}

        // Escape pressed: abandon whatever is in progress.
        virtual void cancel() { reset(); }

        // Drop all pending state.
        virtual void reset() {}

        // Build the in-progress preview given the live cursor position.
        virtual void preview(Pos w, SketchPreview& out) const {}

        // True while a primitive is mid-definition (a click is pending).
        virtual bool active() const { return false; }

        // Whether geometry produced by this tool is *construction* (reference)
        // geometry rather than real output. The view drives this.
        bool construction = false;

        // Stamp the current construction flag onto any in-progress committed
        // primitive (the line tool's growing chain). Tools that commit atomically
        // don't need it.
        virtual void applyConstruction(Project& project) {}
    };

    // Point: each click drops a point. No multi-step state.
    struct PointTool : public Tool {

        void click(Project& project, Pos w) override {
            Point2 p(w);
            p.construction = construction;
            project.addPoint(p);
        }

        void preview(Pos w, SketchPreview& out) const override {
            out.candidate.points.push_back(Point2(w));
        }
    };

    // Line: each click adds a vertex to one continuous polyline. The chain is a
    // real, linked path in the project (extended in place as it grows), so its
    // interior joins are shared vertices -- not a pile of independent segments.
    // Enter finalizes the current chain and starts a fresh one.
    struct LineTool : public Tool {

        std::vector<Point2> pts;     // the current run's vertices
        long activeIndex = -1;       // its polyline index in the project, or -1

        void click(Project& project, Pos w) override {

            pts.push_back(Point2(w));

            Polyline2 line;
            line.points = pts;
            line.construction = construction;

            if (pts.size() == 2) {
                // First real segment: create the polyline and remember it.
                activeIndex = static_cast<long>(project.addPolyline(line));
            }
            else if (pts.size() > 2 && activeIndex >= 0) {
                // Extend the existing chain in place.
                project.replacePolyline(static_cast<size_t>(activeIndex), line);
            }
        }

        void applyConstruction(Project& project) override {
            if (activeIndex >= 0 && static_cast<size_t>(activeIndex) < project.geometry.polylines.size()) {
                project.geometry.polylines[static_cast<size_t>(activeIndex)].construction = construction;
                project.dirty = true;
            }
        }

        void enter(Project& project) override { reset(); }

        void reset() override {
            pts.clear();
            activeIndex = -1;
        }

        bool active() const override { return !pts.empty(); }

        void preview(Pos w, SketchPreview& out) const override {

            if (pts.empty()) { return; }

            // Mark the placed vertices and rubber-band the next segment.
            for (const Point2& p : pts) { out.helper.points.push_back(p); }

            out.candidate.segments.push_back({ pts.back(), Pos2(w) });
        }
    };

    // Circle: click center, click to set radius (commits), repeat.
    struct CircleTool : public Tool {

        bool hasCenter = false;
        Pos c;

        void click(Project& project, Pos w) override {

            if (!hasCenter) { c = w; hasCenter = true; return; }

            Circle2 circle{ c, (w - c).pythag() };
            circle.construction = construction;
            project.addCircle(circle);
            hasCenter = false;
        }

        void enter(Project& project) override { reset(); }

        void reset() override { hasCenter = false; }

        bool active() const override { return hasCenter; }

        void preview(Pos w, SketchPreview& out) const override {
            if (!hasCenter) { return; }
            out.helper.points.push_back(Point2(c));
            out.helper.segments.push_back({ Pos2(c), Pos2(w) });    // ghost radius
            out.candidate.circles.push_back({ c, (w - c).pythag() }); // intent
        }
    };

    // Arc: click center, click A (start, fixes radius), click B (end, commits).
    //
    // Direction intent: rather than guessing CW/CCW from the final B angle (the
    // SolidWorks failure mode, which flips badly near 180 degrees), we integrate
    // the *signed* angular motion of the cursor about the center. Each move we
    // take the signed angle between the previous and current c->cursor directions
    // and accumulate it. The running sum's sign is the sweep direction and its
    // magnitude is the true swept angle (so reflex arcs just fall out naturally).
    // The stored arc is always CCW from start to end, so we order the endpoints by
    // the swept direction rather than storing chirality.
    struct ArcTool : public Tool {

        bool hasCenter = false;
        bool hasA = false;
        Pos c;
        Pos a;

        // B-phase tracking
        float radius = 0.0f;
        float prevAngle = 0.0f;
        float sweep = 0.0f;        // accumulated signed travel, wrapped to (-TAU, TAU)

        void click(Project& project, Pos w) override {

            if (!hasCenter) {
                c = w;
                hasCenter = true;
                return;
            }

            if (!hasA) {
                a = w;
                hasA = true;

                // Seed the sweep integrator at A.
                radius = (a - c).pythag();
                prevAngle = (a - c).angle();
                sweep = 0.0;
                return;
            }

            // Commit the arc, ordering the endpoints so it is always swept CCW.
            Pos s, e;
            resolve(w, s, e);

            Arc2 arc{ c, s, e };
            arc.construction = construction;
            project.addArc(arc);
            reset();
        }

        void hover(Pos w) override {

            if (!hasCenter || !hasA) { return; }

            // Integrate the signed angular step. Wrap, rather than clamp, at a
            // full turn: completing a loop restarts the arc from A while keeping
            // the current chirality, so the user never has to unwind spins.
            float cur = (w - c).angle();

            float step = cur - prevAngle;
            while (step <= -PI) { step += TAU; }   // shortest signed step
            while (step >   PI) { step -= TAU; }

            sweep += step;
            prevAngle = cur;

            if (sweep >  TAU) { sweep -= TAU; }
            if (sweep < -TAU) { sweep += TAU; }
        }

        void enter(Project& project) override { reset(); }

        void reset() override {
            hasCenter = false;
            hasA = false;
            sweep = 0.0;
        }

        bool active() const override { return hasCenter; }

        // The cursor point projected onto the circle (tracks the mouse exactly).
        Pos pointB(Pos w) const {
            Pos dir = w - c;
            float ml = dir.pythag();
            return (ml > 1e-9) ? (c + dir / ml * radius) : a;
        }

        // Resolve the CCW-ordered endpoints start -> end for cursor w. Sweeping CCW
        // from A keeps (A -> B); sweeping CW means the same geometric arc is the CCW
        // one from B to A, so we emit (B -> A). Major/minor then follows from order.
        void resolve(Pos w, Pos& s, Pos& e) const {
            Pos b = pointB(w);
            if (sweep >= 0.0) { s = a; e = b; }
            else              { s = b; e = a; }
        }

        void preview(Pos w, SketchPreview& out) const override {

            if (!hasCenter) { return; }

            out.helper.points.push_back(Point2(c));

            // Defining A: ghost the full circle + radius line at the cursor.
            if (!hasA) {
                out.helper.segments.push_back({ Pos2(c), Pos2(w) });
                out.helper.circles.push_back({ c, (w - c).pythag() });
                out.candidate.points.push_back(Point2(w));
                return;
            }

            // Defining B: ghost circle + spokes underneath; the candidate arc and
            // its end point B on top.
            Pos s, e;
            resolve(w, s, e);
            Pos b = pointB(w);

            out.helper.points.push_back(Point2(a));
            out.helper.segments.push_back({ Pos2(c), Pos2(a) });   // start spoke (A)
            out.helper.segments.push_back({ Pos2(c), Pos2(b) });   // end spoke (B)
            out.helper.circles.push_back({ c, radius });           // ghost circle

            out.candidate.arcs.push_back({ c, s, e });
            out.candidate.points.push_back(Point2(b));             // show point B
        }
    };

    // Box: click a corner, click the opposite corner -> four line segments. Like
    // a rectangle tool in any CAD program.
    struct BoxTool : public Tool {

        bool hasFirst = false;
        Pos f;

        // The four corners of the axis-aligned box from p0 to p1, CCW-ish order.
        static void corners(Pos p0, Pos p1, Pos& a, Pos& b, Pos& c, Pos& d) {
            a = p0;                  // first corner
            b = Pos(p1.x, p0.y);
            c = p1;                  // opposite corner
            d = Pos(p0.x, p1.y);
        }

        void click(Project& project, Pos w) override {

            if (!hasFirst) {
                f = w;
                hasFirst = true;
                return;
            }

            Pos a, b, c, d;
            corners(f, w, a, b, c, d);

            Segment2 s1{ a, b }; s1.construction = construction; project.addSegment(s1);
            Segment2 s2{ b, c }; s2.construction = construction; project.addSegment(s2);
            Segment2 s3{ c, d }; s3.construction = construction; project.addSegment(s3);
            Segment2 s4{ d, a }; s4.construction = construction; project.addSegment(s4);

            hasFirst = false;
        }

        void enter(Project& project) override { reset(); }
        void reset() override { hasFirst = false; }
        bool active() const override { return hasFirst; }

        void preview(Pos w, SketchPreview& out) const override {
            if (!hasFirst) { return; }
            Pos a, b, c, d;
            corners(f, w, a, b, c, d);
            out.helper.points.push_back(Point2(f));
            out.candidate.segments.push_back({ Pos2(a), Pos2(b) });
            out.candidate.segments.push_back({ Pos2(b), Pos2(c) });
            out.candidate.segments.push_back({ Pos2(c), Pos2(d) });
            out.candidate.segments.push_back({ Pos2(d), Pos2(a) });
        }
    };

    // Ellipse: click the centre, then a major-axis point A (sets U = A - C), then a
    // second perimeter point B (sets the conjugate semi-axis V).
    struct EllipseTool : public Tool {

        bool hasCenter = false;
        bool hasMajor  = false;
        Pos c;   // centre C
        Pos a;   // major-axis point A

        void click(Project& project, Pos w) override {

            if (!hasCenter) { c = w; hasCenter = true; return; }

            if (!hasMajor) { a = w; hasMajor = true; return; }

            EllipseFit f = fitEllipse(c, a, w);
            Ellipse2 e{ c, f.u, f.v };
            e.construction = construction;
            project.addEllipse(e);
            reset();
        }

        void enter(Project& project) override { reset(); }
        void reset() override { hasCenter = false; hasMajor = false; }
        bool active() const override { return hasCenter; }

        void preview(Pos w, SketchPreview& out) const override {

            if (!hasCenter) { return; }
            out.helper.points.push_back(Point2(c));

            if (!hasMajor) {
                // Rubber-band the major axis; ghost a circle (V = U rotated 90).
                Pos rad = w - c;
                out.candidate.points.push_back(Point2(w));
                out.candidate.ellipses.push_back({ c, rad, Pos(-rad.y, rad.x) });
                return;
            }

            out.helper.points.push_back(Point2(a));
            EllipseFit f = fitEllipse(c, a, w);
            out.candidate.ellipses.push_back({ c, f.u, f.v });
            out.candidate.points.push_back(Point2(w));
        }
    };

    // Elliptical arc: click the centre, then the major-axis point A (which is also
    // the arc's start), then point B (which fixes the conjugate semi-axis *and* is
    // the arc's end). The swept-parameter integrator (same idea as ArcTool) orders
    // the endpoints so the stored arc is always swept CCW (increasing parameter):
    // A sits at parameter 0, and the sweep is accumulated as B is dragged around.
    struct EllipseArcTool : public Tool {

        bool hasCenter = false;
        bool hasMajor  = false;
        Pos c;   // centre C
        Pos a;   // major-axis point A (arc start, param 0)
        float prevT = 0.0f;
        float sweep = 0.0f;

        // Parameter of B, using the conjugate axis B itself implies.
        float paramOfEnd(Pos w) const {
            EllipseFit f = fitEllipse(c, a, w);
            return ellipseParamOf(c, f.u, f.v, w);
        }

        void click(Project& project, Pos w) override {

            if (!hasCenter) { c = w; hasCenter = true; return; }

            if (!hasMajor) {
                a = w;
                prevT = 0.0; sweep = 0.0;
                hasMajor = true;
                return;
            }

            // A is at parameter 0; B at parameter `sweep`. Store the endpoints so
            // the arc is swept by increasing parameter (CCW): a positive sweep is
            // 0 -> sweep, a negative sweep is the CCW arc sweep -> 0.
            EllipseFit f = fitEllipse(c, a, w);
            float s0 = (sweep >= 0.0f) ? 0.0f : sweep;
            float s1 = (sweep >= 0.0f) ? sweep : 0.0f;
            EllipseArc2 e{ c, f.u, f.v, s0, s1 };
            e.construction = construction;
            project.addEllipseArc(e);
            reset();
        }

        void hover(Pos w) override {
            if (!hasMajor) { return; }
            float t = paramOfEnd(w);
            float step = t - prevT;
            while (step <= -PI) { step += TAU; }
            while (step >   PI) { step -= TAU; }
            sweep += step;
            if (sweep >  TAU) { sweep -= TAU; }
            if (sweep < -TAU) { sweep += TAU; }
            prevT = t;
        }

        void enter(Project& project) override { reset(); }
        void reset() override { hasCenter = false; hasMajor = false; sweep = 0.0; prevT = 0.0; }
        bool active() const override { return hasCenter; }

        void preview(Pos w, SketchPreview& out) const override {

            if (!hasCenter) { return; }
            out.helper.points.push_back(Point2(c));

            if (!hasMajor) {
                Pos rad = w - c;
                out.candidate.points.push_back(Point2(w));
                out.candidate.ellipses.push_back({ c, rad, Pos(-rad.y, rad.x) });
                return;
            }

            out.helper.points.push_back(Point2(a));

            // Ghost the full ellipse B currently implies, plus the candidate arc.
            EllipseFit f = fitEllipse(c, a, w);
            float s0 = (sweep >= 0.0f) ? 0.0f : sweep;
            float s1 = (sweep >= 0.0f) ? sweep : 0.0f;
            out.helper.ellipses.push_back({ c, f.u, f.v });
            out.candidate.ellipseArcs.push_back({ c, f.u, f.v, s0, s1 });

            Pos endp = ellipsePointAt(c, f.u, f.v, sweep);
            out.candidate.points.push_back(Point2(endp));
        }
    };
}
