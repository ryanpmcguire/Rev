module;

#include <cmath>
#include <vector>

export module Sketch.App.Tool;

import Sketch.App.Geometry;
import Sketch.App.Project;

export namespace Sketch::App {

    inline constexpr double PI  = 3.14159265358979323846;
    inline constexpr double TAU = 2.0 * PI;

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

        // A left click landed at world (wx, wy).
        virtual void click(Project& project, double wx, double wy) = 0;

        // The cursor moved to world (wx, wy) while this tool is active. Tools
        // that need to integrate motion (e.g. arc direction) track it here.
        virtual void hover(double wx, double wy) {}

        // Enter pressed: finish/restart the current run.
        virtual void enter(Project& project) {}

        // Escape pressed: abandon whatever is in progress.
        virtual void cancel() { reset(); }

        // Drop all pending state.
        virtual void reset() {}

        // Build the in-progress preview given the live cursor position.
        virtual void preview(double wx, double wy, SketchPreview& out) const {}

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

        void click(Project& project, double wx, double wy) override {
            Point2 p{ wx, wy };
            p.construction = construction;
            project.addPoint(p);
        }

        void preview(double wx, double wy, SketchPreview& out) const override {
            out.candidate.points.push_back({ wx, wy });
        }
    };

    // Line: each click adds a vertex to one continuous polyline. The chain is a
    // real, linked path in the project (extended in place as it grows), so its
    // interior joins are shared vertices -- not a pile of independent segments.
    // Enter finalizes the current chain and starts a fresh one.
    struct LineTool : public Tool {

        std::vector<Point2> pts;     // the current run's vertices
        long activeIndex = -1;       // its polyline index in the project, or -1

        void click(Project& project, double wx, double wy) override {

            pts.push_back({ wx, wy });

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

        void preview(double wx, double wy, SketchPreview& out) const override {

            if (pts.empty()) { return; }

            // Mark the placed vertices and rubber-band the next segment.
            for (const Point2& p : pts) { out.helper.points.push_back(p); }

            const Point2& last = pts.back();
            out.candidate.segments.push_back({ last.x, last.y, wx, wy });
        }
    };

    // Circle: click center, click to set radius (commits), repeat.
    struct CircleTool : public Tool {

        bool hasCenter = false;
        double cx = 0.0, cy = 0.0;

        void click(Project& project, double wx, double wy) override {

            if (!hasCenter) {
                cx = wx; cy = wy;
                hasCenter = true;
                return;
            }

            double r = std::hypot(wx - cx, wy - cy);
            Circle2 c{ cx, cy, r };
            c.construction = construction;
            project.addCircle(c);
            hasCenter = false;
        }

        void enter(Project& project) override { reset(); }

        void reset() override { hasCenter = false; }

        bool active() const override { return hasCenter; }

        void preview(double wx, double wy, SketchPreview& out) const override {
            if (!hasCenter) { return; }
            double r = std::hypot(wx - cx, wy - cy);
            out.helper.points.push_back({ cx, cy });
            out.helper.segments.push_back({ cx, cy, wx, wy });   // ghost radius
            out.candidate.circles.push_back({ cx, cy, r });      // intent
        }
    };

    // Arc: click center, click A (start, fixes radius), click B (end, commits).
    //
    // Direction intent: rather than guessing CW/CCW from the final B angle (the
    // SolidWorks failure mode, which flips badly near 180 degrees), we integrate
    // the *signed* angular motion of the cursor about the center. Each move we
    // take the normalized cross and dot of the previous and current c->cursor
    // directions; atan2(cross, dot) is the small signed step, and we accumulate
    // it. The running sum's sign is the sweep direction and its magnitude is the
    // true swept angle (so reflex arcs and full intent just fall out naturally).
    struct ArcTool : public Tool {

        bool hasCenter = false;
        bool hasA = false;
        double cx = 0.0, cy = 0.0;
        double ax = 0.0, ay = 0.0;

        // B-phase tracking
        double radius = 0.0;
        double prevAngle = 0.0;
        double sweep = 0.0;        // accumulated signed travel, wrapped to (-TAU, TAU)

        void click(Project& project, double wx, double wy) override {

            if (!hasCenter) {
                cx = wx; cy = wy;
                hasCenter = true;
                return;
            }

            if (!hasA) {
                ax = wx; ay = wy;
                hasA = true;

                // Seed the sweep integrator at A.
                radius = std::hypot(ax - cx, ay - cy);
                prevAngle = std::atan2(ay - cy, ax - cx);
                sweep = 0.0;
                return;
            }

            // Commit using the exact-intersection B and the chirality-derived D.
            double bx, by, dx, dy;
            resolve(wx, wy, bx, by, dx, dy);

            Arc2 a{ cx, cy, ax, ay, bx, by, dx, dy };
            a.construction = construction;
            project.addArc(a);
            reset();
        }

        void hover(double wx, double wy) override {

            if (!hasCenter || !hasA) { return; }

            // Integrate the signed angular step (atan2 of the cross/dot of
            // successive c->cursor directions). Wrap, rather than clamp, at a
            // full turn: completing a loop restarts the arc from A while keeping
            // the current chirality, so the user never has to unwind spins.
            double cur = std::atan2(wy - cy, wx - cx);

            double step = cur - prevAngle;
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

        // Resolve the end point B and the chirality point D for cursor (wx, wy).
        //
        // B is defined *strictly* as the intersection of the C->cursor ray with
        // the circle (so it tracks the mouse exactly). D is the midpoint of the
        // arc A->B taken in the swept direction (sign of `sweep`): when the
        // cursor is within a half-turn it sits between A and B (minor arc); once
        // the user sweeps past the antipode the directed midpoint lands on the
        // far side (major arc) -- the chirality is carried entirely by D.
        void resolve(double wx, double wy, double& bx, double& by,
                     double& dx, double& dy) const {

            double mdx = wx - cx, mdy = wy - cy;
            double ml = std::hypot(mdx, mdy);

            if (ml > 1e-9) { bx = cx + radius * mdx / ml; by = cy + radius * mdy / ml; }
            else           { bx = ax; by = ay; }

            double aA = std::atan2(ay - cy, ax - cx);
            double aB = std::atan2(by - cy, bx - cx);

            double ccwSpan = aB - aA;                      // CCW distance A -> B
            while (ccwSpan < 0.0)  { ccwSpan += TAU; }
            while (ccwSpan >= TAU) { ccwSpan -= TAU; }

            // Directed span in the swept direction, then its midpoint.
            double directed = (sweep >= 0.0) ? ccwSpan : (ccwSpan - TAU);
            double midAngle = aA + directed * 0.5;

            dx = cx + radius * std::cos(midAngle);
            dy = cy + radius * std::sin(midAngle);
        }

        void preview(double wx, double wy, SketchPreview& out) const override {

            if (!hasCenter) { return; }

            out.helper.points.push_back({ cx, cy });

            // Defining A: ghost the full circle + radius line at the cursor.
            if (!hasA) {
                double r = std::hypot(wx - cx, wy - cy);
                out.helper.segments.push_back({ cx, cy, wx, wy });
                out.helper.circles.push_back({ cx, cy, r });
                out.candidate.points.push_back({ wx, wy });
                return;
            }

            // Defining B: ghost circle + spokes underneath; the candidate arc and
            // its end point B on top.
            double bx, by, dx, dy;
            resolve(wx, wy, bx, by, dx, dy);

            out.helper.points.push_back({ ax, ay });
            out.helper.segments.push_back({ cx, cy, ax, ay });   // start spoke
            out.helper.segments.push_back({ cx, cy, bx, by });   // end spoke
            out.helper.circles.push_back({ cx, cy, radius });    // ghost circle

            out.candidate.arcs.push_back({ cx, cy, ax, ay, bx, by, dx, dy });
            out.candidate.points.push_back({ bx, by });          // show point B
        }
    };

    // Box: click a corner, click the opposite corner -> four line segments. Like
    // a rectangle tool in any CAD program.
    struct BoxTool : public Tool {

        bool hasFirst = false;
        double fx = 0.0, fy = 0.0;

        static void corners(double x0, double y0, double x1, double y1,
                            double& ax, double& ay, double& bx, double& by,
                            double& ccx, double& ccy, double& dx, double& dy) {
            ax = x0; ay = y0;   // first corner
            bx = x1; by = y0;
            ccx = x1; ccy = y1; // opposite corner
            dx = x0; dy = y1;
        }

        void click(Project& project, double wx, double wy) override {

            if (!hasFirst) {
                fx = wx; fy = wy;
                hasFirst = true;
                return;
            }

            double ax, ay, bx, by, ccx, ccy, dx, dy;
            corners(fx, fy, wx, wy, ax, ay, bx, by, ccx, ccy, dx, dy);

            Segment2 s1{ ax, ay, bx, by };   s1.construction = construction; project.addSegment(s1);
            Segment2 s2{ bx, by, ccx, ccy }; s2.construction = construction; project.addSegment(s2);
            Segment2 s3{ ccx, ccy, dx, dy }; s3.construction = construction; project.addSegment(s3);
            Segment2 s4{ dx, dy, ax, ay };   s4.construction = construction; project.addSegment(s4);

            hasFirst = false;
        }

        void enter(Project& project) override { reset(); }
        void reset() override { hasFirst = false; }
        bool active() const override { return hasFirst; }

        void preview(double wx, double wy, SketchPreview& out) const override {
            if (!hasFirst) { return; }
            double ax, ay, bx, by, ccx, ccy, dx, dy;
            corners(fx, fy, wx, wy, ax, ay, bx, by, ccx, ccy, dx, dy);
            out.helper.points.push_back({ fx, fy });
            out.candidate.segments.push_back({ ax, ay, bx, by });
            out.candidate.segments.push_back({ bx, by, ccx, ccy });
            out.candidate.segments.push_back({ ccx, ccy, dx, dy });
            out.candidate.segments.push_back({ dx, dy, ax, ay });
        }
    };

    // Ellipse (axis-aligned): click centre, click a corner of the bounding box.
    // Ellipse: click the centre, then a major-axis point A (sets the rotation and
    // semi-major axis), then a second perimeter point B (sets the semi-minor axis).
    struct EllipseTool : public Tool {

        bool hasCenter = false;
        bool hasMajor  = false;
        double cx = 0.0, cy = 0.0;     // centre C
        double ax = 0.0, ay = 0.0;     // major-axis point A

        void click(Project& project, double wx, double wy) override {

            if (!hasCenter) { cx = wx; cy = wy; hasCenter = true; return; }

            if (!hasMajor) { ax = wx; ay = wy; hasMajor = true; return; }

            EllipseFit f = fitEllipse(cx, cy, ax, ay, wx, wy);
            Ellipse2 e{ cx, cy, f.ux, f.uy, f.vx, f.vy };
            e.construction = construction;
            project.addEllipse(e);
            reset();
        }

        void enter(Project& project) override { reset(); }
        void reset() override { hasCenter = false; hasMajor = false; }
        bool active() const override { return hasCenter; }

        void preview(double wx, double wy, SketchPreview& out) const override {

            if (!hasCenter) { return; }
            out.helper.points.push_back({ cx, cy });

            if (!hasMajor) {
                // Rubber-band the major axis; ghost a circle of that radius.
                double rx = wx - cx, ry = wy - cy;
                out.candidate.points.push_back({ wx, wy });
                out.candidate.ellipses.push_back({ cx, cy, rx, ry, -ry, rx });
                return;
            }

            out.helper.points.push_back({ ax, ay });
            EllipseFit f = fitEllipse(cx, cy, ax, ay, wx, wy);
            out.candidate.ellipses.push_back({ cx, cy, f.ux, f.uy, f.vx, f.vy });
            out.candidate.points.push_back({ wx, wy });
        }
    };

    // Elliptical arc: click the centre, then the major-axis point A (which is also
    // the arc's start), then point B (which fixes the semi-minor axis *and* is the
    // arc's end). Chirality uses the same swept-parameter integrator as ArcTool, on
    // the ellipse's eccentric anomaly: the start sits at local parameter 0 (point A
    // is on the major axis) and the sweep is accumulated as B is dragged around.
    struct EllipseArcTool : public Tool {

        bool hasCenter = false;
        bool hasMajor  = false;
        double cx = 0.0, cy = 0.0;     // centre C
        double ax = 0.0, ay = 0.0;     // major-axis point A (arc start, param 0)
        double prevT = 0.0;
        double sweep = 0.0;

        // Parameter of B, using the conjugate axis B itself implies.
        double paramOfEnd(double wx, double wy) const {
            EllipseFit f = fitEllipse(cx, cy, ax, ay, wx, wy);
            return ellipseParamOf(cx, cy, f.ux, f.uy, f.vx, f.vy, wx, wy);
        }

        void click(Project& project, double wx, double wy) override {

            if (!hasCenter) { cx = wx; cy = wy; hasCenter = true; return; }

            if (!hasMajor) {
                ax = wx; ay = wy;
                prevT = 0.0; sweep = 0.0;
                hasMajor = true;
                return;
            }

            EllipseFit f = fitEllipse(cx, cy, ax, ay, wx, wy);
            EllipseArc2 e{ cx, cy, f.ux, f.uy, f.vx, f.vy, 0.0, sweep, sweep * 0.5 };
            e.construction = construction;
            project.addEllipseArc(e);
            reset();
        }

        void hover(double wx, double wy) override {
            if (!hasMajor) { return; }
            double t = paramOfEnd(wx, wy);
            double step = t - prevT;
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

        void preview(double wx, double wy, SketchPreview& out) const override {

            if (!hasCenter) { return; }
            out.helper.points.push_back({ cx, cy });

            if (!hasMajor) {
                double rx = wx - cx, ry = wy - cy;
                out.candidate.points.push_back({ wx, wy });
                out.candidate.ellipses.push_back({ cx, cy, rx, ry, -ry, rx });
                return;
            }

            out.helper.points.push_back({ ax, ay });

            // Ghost the full ellipse B currently implies, plus the candidate arc.
            EllipseFit f = fitEllipse(cx, cy, ax, ay, wx, wy);
            out.helper.ellipses.push_back({ cx, cy, f.ux, f.uy, f.vx, f.vy });
            out.candidate.ellipseArcs.push_back({ cx, cy, f.ux, f.uy, f.vx, f.vy, 0.0, sweep, sweep * 0.5 });

            double ex, ey;
            ellipsePointAt(cx, cy, f.ux, f.uy, f.vx, f.vy, sweep, ex, ey);
            out.candidate.points.push_back({ ex, ey });
        }
    };
}
