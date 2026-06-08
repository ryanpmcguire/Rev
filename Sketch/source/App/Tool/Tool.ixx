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

    // Line: each click adds a vertex to one continuous polyline, extended in place.
    struct LineTool : public Tool {

        std::vector<Pos2> pts;       // the current run's vertices
        long activeIndex = -1;       // its entity index in the project, or -1

        void click(Project& project, Pos w) override {

            pts.push_back(w);

            auto line = std::make_unique<Polyline2>();
            line->points = pts;
            line->construction = construction;

            if (pts.size() == 2) {
                activeIndex = static_cast<long>(project.add(std::move(line)));
            }
            else if (pts.size() > 2 && activeIndex >= 0) {
                project.replace(static_cast<size_t>(activeIndex), std::move(line));
            }
        }

        void applyConstruction(Project& project) override {
            if (activeIndex >= 0 && static_cast<size_t>(activeIndex) < project.geometry.entities.size()) {
                project.geometry.entities[static_cast<size_t>(activeIndex)]->construction = construction;
                project.dirty = true;
            }
        }

        void enter(Project& project) override { reset(); }
        void reset() override { pts.clear(); activeIndex = -1; }
        bool active() const override { return !pts.empty(); }

        void preview(Pos w, SketchPreview& out) const override {
            if (pts.empty()) { return; }
            for (const Pos2& p : pts) { out.helper.add(std::make_unique<Point2>(p)); }
            out.candidate.add(std::make_unique<Segment2>(pts.back(), w));
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
    // running sum's sign is the sweep direction. The stored arc is always swept CCW
    // from start to end, so we order the endpoints by the swept direction rather
    // than storing chirality.
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

            Pos s, e;
            resolve(w, s, e);
            commit(project, std::make_unique<Arc2>(c, s, e));
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

        // CCW-ordered endpoints start -> end. Sweeping CCW keeps (A -> B); sweeping
        // CW emits (B -> A), so the stored arc is always the CCW one.
        void resolve(Pos w, Pos& s, Pos& e) const {
            Pos b = pointB(w);
            if (sweep >= 0.0f) { s = a; e = b; }
            else               { s = b; e = a; }
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

            Pos s, e; resolve(w, s, e);
            Pos b = pointB(w);

            out.helper.add(std::make_unique<Point2>(a));
            out.helper.add(std::make_unique<Segment2>(c, a));   // start spoke
            out.helper.add(std::make_unique<Segment2>(c, b));   // end spoke
            out.helper.add(std::make_unique<Circle2>(c, radius));
            out.candidate.add(std::make_unique<Arc2>(c, s, e));
            out.candidate.add(std::make_unique<Point2>(b));
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
    struct EllipseTool : public Tool {

        bool hasCenter = false;
        bool hasMajor  = false;
        Pos c, a;

        void click(Project& project, Pos w) override {
            if (!hasCenter) { c = w; hasCenter = true; return; }
            if (!hasMajor)  { a = w; hasMajor = true; return; }
            EllipseFit f = fitEllipse(c, a, w);
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
            EllipseFit f = fitEllipse(c, a, w);
            out.candidate.add(std::make_unique<Ellipse2>(c, f.u, f.v));
            out.candidate.add(std::make_unique<Point2>(w));
        }
    };

    // Elliptical arc: click centre, major-axis point A (= arc start), point B (=
    // arc end, fixes the minor axis). The swept-parameter integrator orders the
    // endpoints so the stored arc is always swept CCW.
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
            float s0 = (sweep >= 0.0f) ? 0.0f : sweep;
            float s1 = (sweep >= 0.0f) ? sweep : 0.0f;
            commit(project, std::make_unique<EllipseArc2>(c, f.u, f.v, s0, s1));
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
            float s0 = (sweep >= 0.0f) ? 0.0f : sweep;
            float s1 = (sweep >= 0.0f) ? sweep : 0.0f;
            out.helper.add(std::make_unique<Ellipse2>(c, f.u, f.v));
            out.candidate.add(std::make_unique<EllipseArc2>(c, f.u, f.v, s0, s1));
            out.candidate.add(std::make_unique<Point2>(ellipsePointAt(c, f.u, f.v, sweep)));
        }
    };
}
