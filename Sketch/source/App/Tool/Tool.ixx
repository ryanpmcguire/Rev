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
    };

    // Point: each click drops a point. No multi-step state.
    struct PointTool : public Tool {

        void click(Project& project, double wx, double wy) override {
            project.addPoint({ wx, wy });
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

            if (pts.size() == 2) {
                // First real segment: create the polyline and remember it.
                activeIndex = static_cast<long>(project.addPolyline(line));
            }
            else if (pts.size() > 2 && activeIndex >= 0) {
                // Extend the existing chain in place.
                project.replacePolyline(static_cast<size_t>(activeIndex), line);
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
            project.addCircle({ cx, cy, r });
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

            project.addArc({ cx, cy, ax, ay, bx, by, dx, dy });
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
}
