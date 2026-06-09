module;

#include <cmath>
#include <vector>
#include <memory>

export module Sketch.App.Tool;

import Rev.Core.Pos;

import Sketch.App.Geometry;
import Sketch.App.Project;

export namespace Sketch::App {

    using Rev::Core::Pos;

    // Preview geometry, split by the role each piece plays. The view renders each
    // bucket in its own colour:
    //   candidate    - *intent*: what is about to become real geometry
    //   construction - construction-only helpers (reserved; unused for now)
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

        // The cursor moved to world position w while this tool is active.
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

        // Whether geometry produced by this tool is *construction* geometry.
        bool construction = false;

        // Stamp the current construction flag onto any in-progress committed
        // primitive (the line tool's growing chain).
        virtual void applyConstruction(Project& project) {}

        // Commit an entity to the project, stamping the construction flag.
        size_t commit(Project& project, std::unique_ptr<Stoicheion> e) const {
            e->construction = construction;
            return project.add(std::move(e));
        }
    };

    // Point: each click drops a point. No multi-step state.
    struct PointTool : public Tool {

        void click(Project& project, Pos w) override {
            commit(project, std::make_unique<Point2>(w));
        }

        void preview(Pos w, SketchPreview& out) const override {
            out.candidate.add(std::make_unique<Point2>(w));
        }
    };

    // Line: each click drops an *independent* segment from the previous point to
    // the new one. A run is just a sequence of segments whose shared endpoints
    // happen to be coincident -- continuity is emergent, not stored. The tool keeps
    // a list of the segments it created this run only so the construction toggle can
    // flip them together while drawing.
    struct LineTool : public Tool {

        bool hasPrev = false;
        Pos prev;                       // the previous vertex
        std::vector<size_t> created;    // entity indices placed this run

        void click(Project& project, Pos w) override {
            if (hasPrev) {
                created.push_back(commit(project, std::make_unique<Segment2>(prev, w)));
            }
            prev = w;
            hasPrev = true;
        }

        void applyConstruction(Project& project) override {
            for (size_t idx : created) {
                if (idx < project.geometry.entities.size()) {
                    project.geometry.entities[idx]->construction = construction;
                }
            }
            project.dirty = true;
        }

        void enter(Project& project) override { reset(); }
        void reset() override { hasPrev = false; created.clear(); }
        bool active() const override { return hasPrev; }

        void preview(Pos w, SketchPreview& out) const override {
            if (!hasPrev) { return; }
            out.helper.add(std::make_unique<Point2>(prev));
            out.candidate.add(std::make_unique<Segment2>(prev, w));   // rubber-band
        }
    };

    // Circle: click center, click to set radius (commits), repeat.
    struct CircleTool : public Tool {

        bool hasCenter = false;
        Pos c;

        void click(Project& project, Pos w) override {
            if (!hasCenter) { c = w; hasCenter = true; return; }
            commit(project, std::make_unique<Circle2>(c, (w - c).pythag()));
            hasCenter = false;
        }

        void enter(Project& project) override { reset(); }
        void reset() override { hasCenter = false; }
        bool active() const override { return hasCenter; }

        void preview(Pos w, SketchPreview& out) const override {
            if (!hasCenter) { return; }
            out.helper.add(std::make_unique<Point2>(c));
            out.helper.add(std::make_unique<Segment2>(c, w));            // ghost radius
            out.candidate.add(std::make_unique<Circle2>(c, (w - c).pythag()));
        }
    };

    // Arc: click center, click A (start, fixes radius), click B (end, commits).
    //
    // We integrate the *signed* angular motion of the cursor about the center; the
    // running sum's sign is the sweep direction. That direction is stored as the
    // arc's through-point D (at the swept midpoint), so the arc keeps its drawn
    // chirality -- clockwise or counterclockwise -- rather than being normalised.
    struct ArcTool : public Tool {

        bool hasCenter = false;
        bool hasA = false;
        Pos c, a;

        float radius = 0.0f;
        float prevAngle = 0.0f;
        float sweep = 0.0f;          // accumulated signed travel, wrapped to (-TAU, TAU)

        void click(Project& project, Pos w) override {

            if (!hasCenter) { c = w; hasCenter = true; return; }

            if (!hasA) {
                a = w; hasA = true;
                radius = (a - c).pythag();
                prevAngle = (a - c).angle();
                sweep = 0.0f;
                return;
            }

            commit(project, buildArc(w));
            reset();
        }

        void hover(Pos w) override {
            if (!hasCenter || !hasA) { return; }
            float cur = (w - c).angle();
            float step = cur - prevAngle;
            while (step <= -PI) { step += TAU; }
            while (step >   PI) { step -= TAU; }
            sweep += step;
            prevAngle = cur;
            if (sweep >  TAU) { sweep -= TAU; }
            if (sweep < -TAU) { sweep += TAU; }
        }

        void enter(Project& project) override { reset(); }
        void reset() override { hasCenter = false; hasA = false; sweep = 0.0f; }
        bool active() const override { return hasCenter; }

        // Cursor projected onto the circle (tracks the mouse exactly).
        Pos pointB(Pos w) const {
            Pos dir = w - c; float ml = dir.pythag();
            return (ml > 1e-6f) ? (c + dir / ml * radius) : a;
        }

        // Build the arc as drawn: start A and the cursor as endpoints, with the
        // through-point D at the swept midpoint -- so the integrated sweep direction
        // becomes the arc's stored chirality (no endpoint swapping).
        std::unique_ptr<Arc2> buildArc(Pos w) const {
            Pos end = pointB(w);
            Pos d = c + Pos::fromAngle((a - c).angle() + sweep * 0.5f) * radius;
            return std::make_unique<Arc2>(c, a, end, d);
        }

        void preview(Pos w, SketchPreview& out) const override {

            if (!hasCenter) { return; }
            out.helper.add(std::make_unique<Point2>(c));

            if (!hasA) {
                out.helper.add(std::make_unique<Segment2>(c, w));
                out.helper.add(std::make_unique<Circle2>(c, (w - c).pythag()));
                out.candidate.add(std::make_unique<Point2>(w));
                return;
            }

            Pos end = pointB(w);

            out.helper.add(std::make_unique<Point2>(a));
            out.helper.add(std::make_unique<Segment2>(c, a));     // start spoke
            out.helper.add(std::make_unique<Segment2>(c, end));   // end spoke
            out.helper.add(std::make_unique<Circle2>(c, radius));
            out.candidate.add(buildArc(w));
            out.candidate.add(std::make_unique<Point2>(end));
        }
    };

    // Box: click a corner, click the opposite corner -> four line segments.
    struct BoxTool : public Tool {

        bool hasFirst = false;
        Pos f;

        static void corners(Pos p0, Pos p1, Pos& a, Pos& b, Pos& c, Pos& d) {
            a = p0; b = Pos(p1.x, p0.y); c = p1; d = Pos(p0.x, p1.y);
        }

        void click(Project& project, Pos w) override {
            if (!hasFirst) { f = w; hasFirst = true; return; }
            Pos a, b, c, d; corners(f, w, a, b, c, d);
            commit(project, std::make_unique<Segment2>(a, b));
            commit(project, std::make_unique<Segment2>(b, c));
            commit(project, std::make_unique<Segment2>(c, d));
            commit(project, std::make_unique<Segment2>(d, a));
            hasFirst = false;
        }

        void enter(Project& project) override { reset(); }
        void reset() override { hasFirst = false; }
        bool active() const override { return hasFirst; }

        void preview(Pos w, SketchPreview& out) const override {
            if (!hasFirst) { return; }
            Pos a, b, c, d; corners(f, w, a, b, c, d);
            out.helper.add(std::make_unique<Point2>(f));
            out.candidate.add(std::make_unique<Segment2>(a, b));
            out.candidate.add(std::make_unique<Segment2>(b, c));
            out.candidate.add(std::make_unique<Segment2>(c, d));
            out.candidate.add(std::make_unique<Segment2>(d, a));
        }
    };

    // Ellipse: click centre, major-axis point A (U = A - C), perimeter point B (V).
    // A full ellipse has no sweep, so its chirality is which side of the major axis B
    // sits on: V points toward B, so placing B on the CCW side gives a CCW ellipse
    // (u x v > 0), the CW side a CW one -- which then offsets like an arc.
    struct EllipseTool : public Tool {

        bool hasCenter = false;
        bool hasMajor  = false;
        Pos c, a;

        // Orient the fitted minor axis toward the perimeter point (sets chirality).
        static EllipseFit fitOriented(Pos c, Pos a, Pos w) {
            EllipseFit f = fitEllipse(c, a, w);
            if ((w - c).dot((a - c).normal()) < 0.0f) { f.v = f.v * -1.0f; }
            return f;
        }

        void click(Project& project, Pos w) override {
            if (!hasCenter) { c = w; hasCenter = true; return; }
            if (!hasMajor)  { a = w; hasMajor = true; return; }
            EllipseFit f = fitOriented(c, a, w);
            commit(project, std::make_unique<Ellipse2>(c, f.u, f.v));
            reset();
        }

        void enter(Project& project) override { reset(); }
        void reset() override { hasCenter = false; hasMajor = false; }
        bool active() const override { return hasCenter; }

        void preview(Pos w, SketchPreview& out) const override {

            if (!hasCenter) { return; }
            out.helper.add(std::make_unique<Point2>(c));

            if (!hasMajor) {
                Pos rad = w - c;     // ghost circle (V = U rotated 90)
                out.candidate.add(std::make_unique<Point2>(w));
                out.candidate.add(std::make_unique<Ellipse2>(c, rad, Pos(-rad.y, rad.x)));
                return;
            }

            out.helper.add(std::make_unique<Point2>(a));
            EllipseFit f = fitOriented(c, a, w);
            out.candidate.add(std::make_unique<Ellipse2>(c, f.u, f.v));
            out.candidate.add(std::make_unique<Point2>(w));
        }
    };

    // Elliptical arc: click centre, major-axis point A (= arc start), point B (=
    // arc end, fixes the minor axis). The swept-parameter integrator's *sign* is the
    // chirality: sweeping CW flips the minor axis V (so u x v < 0), so the arc keeps
    // its drawn orientation and offsets like an arc -- CCW shrinks, CW grows.
    struct EllipseArcTool : public Tool {

        bool hasCenter = false;
        bool hasMajor  = false;
        Pos c, a;
        float prevT = 0.0f;
        float sweep = 0.0f;

        float paramOfEnd(Pos w) const {
            EllipseFit f = fitEllipse(c, a, w);
            return ellipseParamOf(c, f.u, f.v, w);
        }

        void click(Project& project, Pos w) override {

            if (!hasCenter) { c = w; hasCenter = true; return; }

            if (!hasMajor) { a = w; prevT = 0.0f; sweep = 0.0f; hasMajor = true; return; }

            EllipseFit f = fitEllipse(c, a, w);
            Pos v = (sweep >= 0.0f) ? f.v : (f.v * -1.0f);   // CW intent flips handedness
            commit(project, std::make_unique<EllipseArc2>(c, f.u, v, 0.0f, std::fabs(sweep)));
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
        void reset() override { hasCenter = false; hasMajor = false; sweep = 0.0f; prevT = 0.0f; }
        bool active() const override { return hasCenter; }

        void preview(Pos w, SketchPreview& out) const override {

            if (!hasCenter) { return; }
            out.helper.add(std::make_unique<Point2>(c));

            if (!hasMajor) {
                Pos rad = w - c;
                out.candidate.add(std::make_unique<Point2>(w));
                out.candidate.add(std::make_unique<Ellipse2>(c, rad, Pos(-rad.y, rad.x)));
                return;
            }

            out.helper.add(std::make_unique<Point2>(a));
            EllipseFit f = fitEllipse(c, a, w);
            Pos v = (sweep >= 0.0f) ? f.v : (f.v * -1.0f);   // CW intent flips handedness
            float s1 = std::fabs(sweep);
            out.helper.add(std::make_unique<Ellipse2>(c, f.u, v));
            out.candidate.add(std::make_unique<EllipseArc2>(c, f.u, v, 0.0f, s1));
            out.candidate.add(std::make_unique<Point2>(ellipsePointAt(c, f.u, v, s1)));
        }
    };
}
