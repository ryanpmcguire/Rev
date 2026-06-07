module;

#include <cmath>

export module Sketch.App.Tool;

import Sketch.App.Geometry;
import Sketch.App.Project;

export namespace Sketch::App {

    // A sketch tool *is* a little state machine. Each tool owns its own pending
    // state and decides what a click / enter / escape means, so the view stays a
    // dumb input router instead of a tangle of per-tool branches.
    //
    // Coordinates are world-space; the view handles screen<->world. `preview`
    // fills a transient SketchGeometry that the view renders on top of the
    // committed geometry (using the live cursor position).
    struct Tool {

        virtual ~Tool() = default;

        // A left click landed at world (wx, wy).
        virtual void click(Project& project, double wx, double wy) = 0;

        // Enter pressed: finish/restart the current run.
        virtual void enter(Project& project) {}

        // Escape pressed: abandon whatever is in progress.
        virtual void cancel() { reset(); }

        // Drop all pending state.
        virtual void reset() {}

        // Build the in-progress preview given the live cursor position.
        virtual void preview(double wx, double wy, SketchGeometry& out) const {}

        // True while a primitive is mid-definition (a click is pending).
        virtual bool active() const { return false; }
    };

    // Point: each click drops a point. No multi-step state.
    struct PointTool : public Tool {

        void click(Project& project, double wx, double wy) override {
            project.addPoint({ wx, wy });
        }

        void preview(double wx, double wy, SketchGeometry& out) const override {
            out.points.push_back({ wx, wy });
        }
    };

    // Line: click A, click B (commits A->B), and the new A becomes the B just
    // placed so the next click continues the run. Enter starts a fresh run.
    struct LineTool : public Tool {

        bool hasA = false;
        double ax = 0.0, ay = 0.0;

        void click(Project& project, double wx, double wy) override {

            if (!hasA) {
                ax = wx; ay = wy;
                hasA = true;
                return;
            }

            project.addSegment({ ax, ay, wx, wy });

            // Chain: the next segment starts where this one ended.
            ax = wx; ay = wy;
        }

        void enter(Project& project) override { hasA = false; }

        void reset() override { hasA = false; }

        bool active() const override { return hasA; }

        void preview(double wx, double wy, SketchGeometry& out) const override {
            if (!hasA) { return; }
            out.points.push_back({ ax, ay });
            out.segments.push_back({ ax, ay, wx, wy });
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

        void preview(double wx, double wy, SketchGeometry& out) const override {
            if (!hasCenter) { return; }
            double r = std::hypot(wx - cx, wy - cy);
            out.points.push_back({ cx, cy });
            out.circles.push_back({ cx, cy, r });
        }
    };

    // Arc: click center, click A (start, fixes radius), click B (end, commits).
    // The empty slot with highest precedence (center, then A, then B) is what the
    // next click fills, so the order is unambiguous and self-explanatory.
    struct ArcTool : public Tool {

        bool hasCenter = false;
        bool hasA = false;
        double cx = 0.0, cy = 0.0;
        double ax = 0.0, ay = 0.0;

        void click(Project& project, double wx, double wy) override {

            if (!hasCenter) {
                cx = wx; cy = wy;
                hasCenter = true;
                return;
            }

            if (!hasA) {
                ax = wx; ay = wy;
                hasA = true;
                return;
            }

            project.addArc({ cx, cy, ax, ay, wx, wy });
            reset();
        }

        void enter(Project& project) override { reset(); }

        void reset() override { hasCenter = false; hasA = false; }

        bool active() const override { return hasCenter; }

        void preview(double wx, double wy, SketchGeometry& out) const override {

            if (!hasCenter) { return; }

            out.points.push_back({ cx, cy });

            if (!hasA) {
                // Defining A: show the radius rubber-band from center to cursor.
                out.segments.push_back({ cx, cy, wx, wy });
                return;
            }

            // Defining B: show the arc from A to the cursor angle, plus the
            // radius spoke to A for reference.
            out.points.push_back({ ax, ay });
            out.segments.push_back({ cx, cy, ax, ay });
            out.arcs.push_back({ cx, cy, ax, ay, wx, wy });
        }
    };
}
