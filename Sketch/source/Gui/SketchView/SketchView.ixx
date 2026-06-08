module;

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <iterator>
#include <utility>

export module Sketch.Gui.SketchView;

import Rev.Core.Vertex;
import Rev.Core.Color;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;
import Rev.Element.Box;

import Rev.Primitive.FastLines;
import Rev.Graphics.Canvas;

import Sketch.App;
import Sketch.App.Project;
import Sketch.App.Tool;
import Sketch.Gui.Theme;

export namespace Sketch::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    using Rev::Core::Vertex;
    using Rev::Core::Color;

    using Sketch::App::SketchGeometry;
    using Sketch::App::SketchTool;

    // A 2D sketch canvas: the flat cousin of the world view.
    //
    // Camera is CPU-only for now: a world origin (pixels from the element's
    // top-left) plus a zoom (pixels per world unit), mapped to screen by hand.
    //
    //   - Right-drag  : pan
    //   - Mouse wheel : zoom, keeping the point under the cursor invariant
    //   - Left click  : forwarded to the active tool's state machine
    //   - Enter / Esc : forwarded to the active tool (finish / cancel)
    //
    // The view is a dumb input router: each tool owns its own interaction state.
    struct SketchView : public Box {

        static constexpr double TAU = 6.283185307179586;

        Sketch::App::AppState* app = nullptr;

        FastLines* axes = nullptr;       // gnomon: world X / Y axes
        FastLines* geometry = nullptr;   // committed geometry + live preview
        FastLines* highlight = nullptr;  // hover/selection overdraw (orange)

        // A stable reference to one selectable curve. Indices (not pointers) so a
        // selection survives appending new geometry (push_back never invalidates
        // earlier indices). `sub` is the segment within a polyline.
        struct SegmentRef {
            enum class Kind { PolySeg, Segment, Circle, Arc, Ellipse, EllipseArc };
            Kind kind = Kind::Segment;
            size_t index = 0;
            size_t sub = 0;
            bool operator==(const SegmentRef& o) const {
                return kind == o.kind && index == o.index && sub == o.sub;
            }
        };

        std::vector<SegmentRef> selected;
        SegmentRef hovered;
        bool hoverValid = false;
        bool highlightDirty = true;

        // Whether new geometry is being drawn as construction (reference) geometry.
        bool drawingConstruction = false;

        // Tool state machines (owned; selection mirrors app->activeTool).
        Sketch::App::PointTool      pointTool;
        Sketch::App::LineTool       lineTool;
        Sketch::App::CircleTool     circleTool;
        Sketch::App::ArcTool        arcTool;
        Sketch::App::BoxTool        boxTool;
        Sketch::App::EllipseTool    ellipseTool;
        Sketch::App::EllipseArcTool ellipseArcTool;
        SketchTool lastTool = SketchTool::None;

        // CPU camera
        float originX = 0.0f, originY = 0.0f;   // screen px of world (0,0)
        float scale   = 40.0f;                  // pixels per world unit
        bool  initialized = false;

        // Origin captured at the start of a pan (e.mouse.diff is cumulative).
        float panOriginX = 0.0f, panOriginY = 0.0f;

        // Live cursor in world space (drives tool previews).
        double cursorX = 0.0, cursorY = 0.0;

        // Geometry is built in *world* space and uploaded only when it actually
        // changes; pan/zoom just swaps the GPU transform (no CPU rebuild).
        bool geometryDirty = true;
        Sketch::App::Project* lastProject = nullptr;

        SketchView(Element* parent, StyleList styles = {}) : Box(parent, styles, "SketchView") {

            app = Sketch::App::AppState::Get(shared->state);

            this->style->size = { .width = Grow(), .height = Grow() };
            this->style->overflow = Overflow::Hide;
            this->style->background.color = rgba(20, 20, 20, 1.0);

            Graphics::Canvas* canvas = shared->canvas;

            // Axes: 2px, full sharpness (crisp, minimal fringe).
            axes = new FastLines(canvas);
            axes->strokeWidth = 2.0f;
            axes->smoothing = 0.0f;

            // Geometry: 2px white, mild antialiasing (medium sharpness).
            geometry = new FastLines(canvas);
            geometry->strokeWidth = 2.0f;
            geometry->smoothing = 0.6f;

            // Highlight: thicker orange overdraw for hover / selection.
            highlight = new FastLines(canvas);
            highlight->smoothing = 0.8f;
        }

        ~SketchView() {
            delete axes;
            delete geometry;
            delete highlight;
        }

        // Tools
        //--------------------------------------------------

        Sketch::App::Tool* currentTool() {
            switch (app ? app->activeTool : SketchTool::None) {
                case SketchTool::Point:      return &pointTool;
                case SketchTool::Line:       return &lineTool;
                case SketchTool::Circle:     return &circleTool;
                case SketchTool::Arc:        return &arcTool;
                case SketchTool::Box:        return &boxTool;
                case SketchTool::Ellipse:    return &ellipseTool;
                case SketchTool::EllipseArc: return &ellipseArcTool;
                default:                     return nullptr;
            }
        }

        // Reset the previously-active tool whenever the selection changes, so a
        // half-finished primitive doesn't leak across tool switches.
        void syncTool() {

            SketchTool now = app ? app->activeTool : SketchTool::None;

            if (now == lastTool) { return; }

            switch (lastTool) {
                case SketchTool::Point:      pointTool.reset();      break;
                case SketchTool::Line:       lineTool.reset();       break;
                case SketchTool::Circle:     circleTool.reset();     break;
                case SketchTool::Arc:        arcTool.reset();        break;
                case SketchTool::Box:        boxTool.reset();        break;
                case SketchTool::Ellipse:    ellipseTool.reset();    break;
                case SketchTool::EllipseArc: ellipseArcTool.reset(); break;
                default: break;
            }

            lastTool = now;
            geometryDirty = true;
        }

        // CPU transforms
        //--------------------------------------------------

        Vertex worldToScreen(double wx, double wy) const {
            float sx = rect.x + originX + static_cast<float>(wx) * scale;
            float sy = rect.y + originY - static_cast<float>(wy) * scale;
            return Vertex(sx, sy);
        }

        void screenToWorld(float sx, float sy, double& wx, double& wy) const {
            wx =  (sx - rect.x - originX) / scale;
            wy = -(sy - rect.y - originY) / scale;
        }

        // Events
        //--------------------------------------------------

        void mouseDown(Event& e) override {

            Box::mouseDown(e);
            if (!e.propagate) { return; }

            // Left click: draw with the active tool, or select if no tool.
            if (e.mouse.lb) {

                syncTool();

                Sketch::App::Tool* tool = currentTool();

                if (tool) {
                    if (app && app->activeProject) {
                        double wx, wy;
                        screenToWorld(e.mouse.pos.x, e.mouse.pos.y, wx, wy);
                        applySnap(wx, wy);          // magnetism: snap the placed point
                        tool->construction = drawingConstruction;
                        tool->click(*app->activeProject, wx, wy);
                        geometryDirty = true;
                        refresh(e);
                        e.propagate = false;
                    }
                }
                else {
                    handleSelectClick(e);
                }
            }

            // Right button begins a pan: pin the current origin.
            if (e.mouse.rb) {
                panOriginX = originX;
                panOriginY = originY;
            }
        }

        void mouseMove(Event& e) override {

            screenToWorld(e.mouse.pos.x, e.mouse.pos.y, cursorX, cursorY);

            // Skip while panning (right button), so a pan doesn't rebuild anything.
            if (!e.mouse.rb) {

                if (Sketch::App::Tool* tool = currentTool()) {
                    // Drawing: snap the cursor onto nearby features, then preview.
                    applySnap(cursorX, cursorY);
                    tool->hover(cursorX, cursorY);
                    geometryDirty = true;
                    if (hoverValid) { hoverValid = false; highlightDirty = true; }
                    refresh(e);
                }
                else {
                    // Selection mode: hover-highlight the nearest curve.
                    SegmentRef ref;
                    bool hit = findHit(cursorX, cursorY, hitTolerance(), ref);
                    if (hit != hoverValid || (hit && !(ref == hovered))) {
                        hoverValid = hit;
                        hovered = ref;
                        highlightDirty = true;
                        refresh(e);
                    }
                }
            }

            Box::mouseMove(e);

            // With a tool active, hide the OS cursor -- the candidate point dot
            // stands in for it (and makes snapping visible).
            if (currentTool()) { e.mouse.cursor = Cursor::None; }
        }

        void mouseDrag(Event& e) override {

            // Pan with the right button. e.mouse.diff is the total offset from
            // the press point, so anchor to the pinned origin.
            if (e.mouse.rb) {
                originX = panOriginX + e.mouse.diff.x;
                originY = panOriginY + e.mouse.diff.y;
                refresh(e);
                e.propagate = false;
                return;
            }

            Box::mouseDrag(e);
        }

        void mouseWheel(Event& e) override {

            if (e.mouse.wheel.y == 0) { return Box::mouseWheel(e); }

            // World point currently under the cursor (the invariant).
            double wx, wy;
            screenToWorld(e.mouse.pos.x, e.mouse.pos.y, wx, wy);

            float factor = (e.mouse.wheel.y > 0) ? 1.1f : (1.0f / 1.1f);
            scale *= factor;
            scale = std::clamp(scale, 2.0f, 4000.0f);

            // Re-anchor the origin so (wx, wy) stays under the cursor.
            originX = e.mouse.pos.x - rect.x - static_cast<float>(wx) * scale;
            originY = e.mouse.pos.y - rect.y + static_cast<float>(wy) * scale;

            refresh(e);
            e.propagate = false;
        }

        void keyDown(Event& e) override {

            // Adjust the generic line thickness setting.
            if (app && (e.keyboard.arrows.up || e.keyboard.arrows.down)) {
                float step = e.keyboard.arrows.up ? 0.5f : -0.5f;
                app->lineThickness = std::clamp(app->lineThickness + step, 0.5f, 20.0f);
                geometryDirty = true;
                refresh(e);
                e.propagate = false;
                return;
            }

            // Delete the current selection.
            if (e.keyboard.del || e.keyboard.backspace) {
                if (deleteSelected()) {
                    geometryDirty = true;
                    highlightDirty = true;
                    refresh(e);
                    e.propagate = false;
                    return;
                }
            }

            // Frame all geometry.
            if (e.keyboard.key == "f") {
                zoomToFit();
                refresh(e);
                e.propagate = false;
                return;
            }

            // Construction toggle. With a selection: flip those primitives. With
            // none: flip the "drawing construction" mode and re-stamp the current
            // tool's in-progress geometry (and all future additions inherit it).
            if (e.keyboard.key == "c") {
                if (!selected.empty()) {
                    toggleSelectedConstruction();
                }
                else {
                    drawingConstruction = !drawingConstruction;
                    if (Sketch::App::Tool* tool = currentTool()) {
                        tool->construction = drawingConstruction;
                        if (app && app->activeProject) { tool->applyConstruction(*app->activeProject); }
                    }
                }
                geometryDirty = true;
                highlightDirty = true;
                refresh(e);
                e.propagate = false;
                return;
            }

            // Esc clears the selection when not in a drawing tool.
            if (e.keyboard.escape && !currentTool() && !selected.empty()) {
                selected.clear();
                hoverValid = false;
                highlightDirty = true;
                refresh(e);
                e.propagate = false;
                return;
            }

            Sketch::App::Tool* tool = currentTool();

            if (tool && app && app->activeProject) {

                if (e.keyboard.enter) {
                    tool->enter(*app->activeProject);
                    geometryDirty = true;
                    refresh(e);
                    e.propagate = false;
                    return;
                }

                if (e.keyboard.escape) {
                    // First escape cancels an in-progress primitive; a second
                    // escape (nothing in progress) drops the tool entirely, so
                    // we fall back to selection mode.
                    if (tool->active()) {
                        tool->cancel();
                    }
                    else if (app) {
                        app->activeTool = SketchTool::None;
                    }
                    geometryDirty = true;
                    refresh(e);
                    e.propagate = false;
                    return;
                }
            }

            Box::keyDown(e);
        }

        // Build + draw
        //--------------------------------------------------

        void buildAxes() {

            axes->lines.clear();

            float axisY = rect.y + originY;   // world y = 0  (horizontal line)
            float axisX = rect.x + originX;   // world x = 0  (vertical line)

            Color xColor{ 0.85f, 0.33f, 0.33f, 1.0f };
            Color yColor{ 0.40f, 0.78f, 0.47f, 1.0f };

            axes->lines.push_back({
                .points = { Vertex(rect.x, axisY), Vertex(rect.x + rect.w, axisY) },
                .color = xColor
            });

            axes->lines.push_back({
                .points = { Vertex(axisX, rect.y), Vertex(axisX, rect.y + rect.h) },
                .color = yColor
            });

            axes->compute();
        }

        // Push a 2-point world-space segment into a target primitive.
        void appendSeg(FastLines* dst, double ax, double ay, double bx, double by, Color color) {
            dst->lines.push_back({
                .points = {
                    Vertex(static_cast<float>(ax), static_cast<float>(ay)),
                    Vertex(static_cast<float>(bx), static_cast<float>(by))
                },
                .color = color
            });
        }

        // A point renders as a single, very short, thick line: the round caps turn
        // it into a crisp dot. A per-line strokeWidth override keeps it a constant
        // pixel size regardless of zoom (the length is zero, so only the caps show).
        void appendPoint(FastLines* dst, const Sketch::App::Point2& p, Color color) {
            Vertex v(static_cast<float>(p.x), static_cast<float>(p.y));
            float sizePx = (app ? app->lineThickness : 2.0f) * 2.5f;
            dst->lines.push_back({ .points = { v, v }, .color = color, .strokeWidth = sizePx });
        }

        // Sample a circular span of `span` radians starting at a0, in world
        // coordinates (the GPU transform maps it to pixels).
        void appendArcSpan(FastLines* dst, double cx, double cy, double r, double a0, double span, Color color) {

            if (r <= 0.0 || std::fabs(span) < 1e-9) { return; }

            int steps = std::max(2, static_cast<int>(std::ceil(std::fabs(span) / (TAU / 6400.0))));

            std::vector<Vertex> pts;
            pts.reserve(steps + 1);

            for (int i = 0; i <= steps; i++) {
                double a = a0 + span * (static_cast<double>(i) / steps);
                pts.push_back(Vertex(
                    static_cast<float>(cx + r * std::cos(a)),
                    static_cast<float>(cy + r * std::sin(a))
                ));
            }

            dst->lines.push_back({ .points = std::move(pts), .color = color });
        }

        // Signed span A -> B passing through D (chirality implicit in D).
        static double arcSpan(const Sketch::App::Arc2& a) {
            auto norm = [](double x) { while (x < 0.0) { x += TAU; } while (x >= TAU) { x -= TAU; } return x; };
            double aA = std::atan2(a.ay - a.cy, a.ax - a.cx);
            double aB = std::atan2(a.by - a.cy, a.bx - a.cx);
            double aD = std::atan2(a.dy - a.cy, a.dx - a.cx);
            double spanCCW = norm(aB - aA);
            double dD      = norm(aD - aA);
            return (dD <= spanCCW) ? spanCCW : (spanCCW - TAU);
        }

        void appendArc(FastLines* dst, const Sketch::App::Arc2& a, Color color) {
            double r = std::hypot(a.ax - a.cx, a.ay - a.cy);
            if (r <= 0.0) { return; }
            double aA = std::atan2(a.ay - a.cy, a.ax - a.cx);
            appendArcSpan(dst, a.cx, a.cy, r, aA, arcSpan(a), color);
        }

        // Sample an elliptical span (eccentric anomaly t0 .. t0+span).
        void appendEllipseSpan(FastLines* dst, double cx, double cy, double rx, double ry,
                               double t0, double span, Color color) {
            if ((rx <= 0.0 && ry <= 0.0) || std::fabs(span) < 1e-9) { return; }
            int steps = std::max(2, static_cast<int>(std::ceil(std::fabs(span) / (TAU / 6400.0))));
            std::vector<Vertex> pts;
            pts.reserve(steps + 1);
            for (int i = 0; i <= steps; i++) {
                double t = t0 + span * (static_cast<double>(i) / steps);
                pts.push_back(Vertex(
                    static_cast<float>(cx + rx * std::cos(t)),
                    static_cast<float>(cy + ry * std::sin(t))
                ));
            }
            dst->lines.push_back({ .points = std::move(pts), .color = color });
        }

        void appendEllipse(FastLines* dst, const Sketch::App::Ellipse2& e, Color color) {
            appendEllipseSpan(dst, e.cx, e.cy, e.rx, e.ry, 0.0, TAU, color);
        }

        // Signed sweep a0 -> a1 passing through ad (chirality implicit in ad).
        static double ellipseArcSpan(const Sketch::App::EllipseArc2& e) {
            auto norm = [](double x) { while (x < 0.0) { x += TAU; } while (x >= TAU) { x -= TAU; } return x; };
            double spanCCW = norm(e.a1 - e.a0);
            double dD      = norm(e.ad - e.a0);
            return (dD <= spanCCW) ? spanCCW : (spanCCW - TAU);
        }

        void appendEllipseArc(FastLines* dst, const Sketch::App::EllipseArc2& e, Color color) {
            appendEllipseSpan(dst, e.cx, e.cy, e.rx, e.ry, e.a0, ellipseArcSpan(e), color);
        }

        void appendGeometry(FastLines* dst, const SketchGeometry& g, Color color) {

            // Polylines are continuous paths: one Line each, so the primitive
            // miters their interior joins.
            for (const Sketch::App::Polyline2& pl : g.polylines) {

                if (pl.points.size() < 2) { continue; }

                std::vector<Vertex> pts;
                pts.reserve(pl.points.size());
                for (const Sketch::App::Point2& p : pl.points) {
                    pts.push_back(Vertex(static_cast<float>(p.x), static_cast<float>(p.y)));
                }

                dst->lines.push_back({ .points = std::move(pts), .color = color });
            }

            for (const Sketch::App::Segment2& s : g.segments) {
                appendSeg(dst, s.ax, s.ay, s.bx, s.by, color);
            }

            for (const Sketch::App::Circle2& c : g.circles) {
                appendArcSpan(dst, c.cx, c.cy, c.r, 0.0, TAU, color);
            }

            for (const Sketch::App::Arc2& a : g.arcs) {
                appendArc(dst, a, color);
            }
            for (const Sketch::App::Ellipse2& e : g.ellipses) {
                appendEllipse(dst, e, color);
            }
            for (const Sketch::App::EllipseArc2& e : g.ellipseArcs) {
                appendEllipseArc(dst, e, color);
            }

            // Points last so their markers sit on top.
            for (const Sketch::App::Point2& p : g.points) {
                appendPoint(dst, p, color);
            }
        }

        // Render committed geometry, colouring each primitive by whether it is
        // construction (grey) or real (white).
        void appendCommitted(FastLines* dst, const SketchGeometry& g, Color real, Color cons) {

            for (const Sketch::App::Polyline2& pl : g.polylines) {
                if (pl.points.size() < 2) { continue; }
                std::vector<Vertex> pts;
                pts.reserve(pl.points.size());
                for (const Sketch::App::Point2& p : pl.points) {
                    pts.push_back(Vertex(static_cast<float>(p.x), static_cast<float>(p.y)));
                }
                dst->lines.push_back({ .points = std::move(pts), .color = pl.construction ? cons : real });
            }

            for (const Sketch::App::Segment2& s : g.segments) {
                appendSeg(dst, s.ax, s.ay, s.bx, s.by, s.construction ? cons : real);
            }
            for (const Sketch::App::Circle2& c : g.circles) {
                appendArcSpan(dst, c.cx, c.cy, c.r, 0.0, TAU, c.construction ? cons : real);
            }
            for (const Sketch::App::Arc2& a : g.arcs) {
                appendArc(dst, a, a.construction ? cons : real);
            }
            for (const Sketch::App::Ellipse2& e : g.ellipses) {
                appendEllipse(dst, e, e.construction ? cons : real);
            }
            for (const Sketch::App::EllipseArc2& e : g.ellipseArcs) {
                appendEllipseArc(dst, e, e.construction ? cons : real);
            }
            for (const Sketch::App::Point2& p : g.points) {
                appendPoint(dst, p, p.construction ? cons : real);
            }
        }

        // Hit-testing (SDF distance in world space)
        //--------------------------------------------------

        static double distToSegment(double px, double py, double ax, double ay, double bx, double by) {
            double dx = bx - ax, dy = by - ay;
            double len2 = dx * dx + dy * dy;
            double t = (len2 > 1e-12) ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
            t = std::clamp(t, 0.0, 1.0);
            return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
        }

        static double distToArc(double px, double py, const Sketch::App::Arc2& a) {
            double r = std::hypot(a.ax - a.cx, a.ay - a.cy);
            if (r <= 0.0) { return std::hypot(px - a.ax, py - a.ay); }
            auto norm = [](double x) { while (x < 0.0) { x += TAU; } while (x >= TAU) { x -= TAU; } return x; };
            double aA = std::atan2(a.ay - a.cy, a.ax - a.cx);
            double span = arcSpan(a);                       // signed
            double aP = std::atan2(py - a.cy, px - a.cx);
            // Is the cursor angle within the swept range?
            bool within = (span >= 0.0)
                ? (norm(aP - aA) <= span)
                : (norm(aA - aP) <= -span);
            if (within) { return std::fabs(std::hypot(px - a.cx, py - a.cy) - r); }
            return std::min(std::hypot(px - a.ax, py - a.ay), std::hypot(px - a.bx, py - a.by));
        }

        // Nearest point on an elliptical span (no closed form), via coarse
        // sampling. Returns the distance and writes the snap point.
        static double nearestOnEllipse(double px, double py, double cx, double cy,
                                       double rx, double ry, double t0, double span,
                                       double& sx, double& sy) {
            if (rx <= 0.0 && ry <= 0.0) { sx = cx; sy = cy; return std::hypot(px - cx, py - cy); }
            const int N = 96;
            double best = 1e30;
            for (int i = 0; i <= N; i++) {
                double t = t0 + span * (static_cast<double>(i) / N);
                double ex = cx + rx * std::cos(t);
                double ey = cy + ry * std::sin(t);
                double d = std::hypot(px - ex, py - ey);
                if (d < best) { best = d; sx = ex; sy = ey; }
            }
            return best;
        }

        // Selection priority. tier: 2 = axis, 1 = real, 0 = construction. Points
        // (and line endpoints) outrank lines within a tier. Higher wins; we pick
        // the highest-scoring participant, breaking ties by distance -- not simply
        // the nearest. The tier comes from the stored `construction` flag, so a
        // primitive's score is set at birth and changes when it is toggled.
        static int participantScore(int tier, bool isPoint) {
            return tier * 2 + (isPoint ? 1 : 0);
        }

        // Find the highest-priority selectable curve within `thresh`, if any.
        bool findHit(double wx, double wy, double thresh, SegmentRef& out) const {

            if (!app || !app->activeProject) { return false; }
            const SketchGeometry& g = app->activeProject->geometry;

            int bestScore = -1; double bestDist = 1e30; bool found = false;

            auto consider = [&](int tier, bool isPoint, double d, SegmentRef ref) {
                if (d >= thresh) { return; }
                int score = participantScore(tier, isPoint);
                if (!found || score > bestScore || (score == bestScore && d < bestDist)) {
                    bestScore = score; bestDist = d; out = ref; found = true;
                }
            };

            // A segment scores as a point if the cursor is within reach of an end.
            auto segHit = [&](int tier, double ax, double ay, double bx, double by, SegmentRef ref) {
                double d = distToSegment(wx, wy, ax, ay, bx, by);
                bool isPoint = std::hypot(wx - ax, wy - ay) < thresh || std::hypot(wx - bx, wy - by) < thresh;
                consider(tier, isPoint, d, ref);
            };

            for (size_t i = 0; i < g.polylines.size(); i++) {
                int tier = g.polylines[i].construction ? 0 : 1;
                const auto& pts = g.polylines[i].points;
                for (size_t s = 0; s + 1 < pts.size(); s++) {
                    segHit(tier, pts[s].x, pts[s].y, pts[s + 1].x, pts[s + 1].y, { SegmentRef::Kind::PolySeg, i, s });
                }
            }
            for (size_t i = 0; i < g.segments.size(); i++) {
                const auto& s = g.segments[i];
                segHit(s.construction ? 0 : 1, s.ax, s.ay, s.bx, s.by, { SegmentRef::Kind::Segment, i, 0 });
            }
            for (size_t i = 0; i < g.circles.size(); i++) {
                const auto& c = g.circles[i];
                double d = std::fabs(std::hypot(wx - c.cx, wy - c.cy) - c.r);
                consider(c.construction ? 0 : 1, false, d, { SegmentRef::Kind::Circle, i, 0 });
            }
            for (size_t i = 0; i < g.arcs.size(); i++) {
                const auto& a = g.arcs[i];
                double d = distToArc(wx, wy, a);
                bool isPoint = std::hypot(wx - a.ax, wy - a.ay) < thresh || std::hypot(wx - a.bx, wy - a.by) < thresh;
                consider(a.construction ? 0 : 1, isPoint, d, { SegmentRef::Kind::Arc, i, 0 });
            }
            for (size_t i = 0; i < g.ellipses.size(); i++) {
                const auto& e = g.ellipses[i];
                double sx, sy;
                double d = nearestOnEllipse(wx, wy, e.cx, e.cy, e.rx, e.ry, 0.0, TAU, sx, sy);
                consider(e.construction ? 0 : 1, false, d, { SegmentRef::Kind::Ellipse, i, 0 });
            }
            for (size_t i = 0; i < g.ellipseArcs.size(); i++) {
                const auto& e = g.ellipseArcs[i];
                double span = ellipseArcSpan(e);
                double sx, sy;
                double d = nearestOnEllipse(wx, wy, e.cx, e.cy, e.rx, e.ry, e.a0, span, sx, sy);
                double te = e.a0 + span;
                bool isPoint =
                    std::hypot(wx - (e.cx + e.rx * std::cos(e.a0)), wy - (e.cy + e.ry * std::sin(e.a0))) < thresh ||
                    std::hypot(wx - (e.cx + e.rx * std::cos(te)),    wy - (e.cy + e.ry * std::sin(te)))    < thresh;
                consider(e.construction ? 0 : 1, isPoint, d, { SegmentRef::Kind::EllipseArc, i, 0 });
            }
            return found;
        }

        // Pick tolerance in world units (a few pixels, zoom-independent).
        double hitTolerance() const { return 6.0 / scale; }

        bool isSelected(const SegmentRef& ref) const {
            for (const SegmentRef& s : selected) { if (s == ref) { return true; } }
            return false;
        }

        // Snapping ("magnetism")
        //--------------------------------------------------

        static void closestOnSegment(double px, double py, double ax, double ay,
                                     double bx, double by, double& fx, double& fy) {
            double dx = bx - ax, dy = by - ay;
            double len2 = dx * dx + dy * dy;
            double t = (len2 > 1e-12) ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
            t = std::clamp(t, 0.0, 1.0);
            fx = ax + t * dx;
            fy = ay + t * dy;
        }

        // Radial projection of (px,py) onto an arc, valid only within its sweep
        // (endpoints are handled as plain snap points elsewhere).
        static bool snapOnArc(double px, double py, const Sketch::App::Arc2& a, double& sx, double& sy) {
            double r = std::hypot(a.ax - a.cx, a.ay - a.cy);
            if (r <= 0.0) { return false; }
            auto norm = [](double x) { while (x < 0.0) { x += TAU; } while (x >= TAU) { x -= TAU; } return x; };
            double aA = std::atan2(a.ay - a.cy, a.ax - a.cx);
            double span = arcSpan(a);
            double aP = std::atan2(py - a.cy, px - a.cx);
            bool within = (span >= 0.0) ? (norm(aP - aA) <= span) : (norm(aA - aP) <= -span);
            if (!within) { return false; }
            double dd = std::hypot(px - a.cx, py - a.cy);
            if (dd < 1e-9) { sx = a.ax; sy = a.ay; return true; }
            sx = a.cx + (px - a.cx) / dd * r;
            sy = a.cy + (py - a.cy) / dd * r;
            return true;
        }

        // Magnetism for point placement: nudge a world position onto the highest
        // priority snappable participant within reach -- not simply the nearest.
        // Same scoring as selection: axis (tier 2) > real (1) > construction (0),
        // and points/endpoints/centres outrank curve projections within a tier.
        //   point  -> the point itself
        //   line   -> perpendicular foot on the segment
        //   circle -> radial point on the ring
        //   arc    -> radial point on the arc (within its sweep)
        // The world axes (y=0, x=0) and the origin are tier-2 "invisible" features
        // and therefore win over real geometry.
        void applySnap(double& wx, double& wy) const {

            double thresh = hitTolerance();
            int bestScore = -1; double bestDist = 1e30; double bx = wx, by = wy; bool found = false;

            auto consider = [&](int tier, bool isPoint, double d, double sx, double sy) {
                if (d >= thresh) { return; }
                int score = participantScore(tier, isPoint);
                if (!found || score > bestScore || (score == bestScore && d < bestDist)) {
                    bestScore = score; bestDist = d; bx = sx; by = sy; found = true;
                }
            };
            auto pt = [&](int tier, double px, double py) {
                consider(tier, true, std::hypot(wx - px, wy - py), px, py);
            };
            auto seg = [&](int tier, double ax, double ay, double bx2, double by2) {
                double fx, fy; closestOnSegment(wx, wy, ax, ay, bx2, by2, fx, fy);
                consider(tier, false, std::hypot(wx - fx, wy - fy), fx, fy);
            };

            if (app && app->activeProject) {
                const SketchGeometry& g = app->activeProject->geometry;

                for (const auto& p : g.points) { pt(p.construction ? 0 : 1, p.x, p.y); }

                for (const auto& pl : g.polylines) {
                    int tier = pl.construction ? 0 : 1;
                    for (const auto& p : pl.points) { pt(tier, p.x, p.y); }
                    for (size_t s = 0; s + 1 < pl.points.size(); s++) {
                        seg(tier, pl.points[s].x, pl.points[s].y, pl.points[s + 1].x, pl.points[s + 1].y);
                    }
                }
                for (const auto& s : g.segments) {
                    int tier = s.construction ? 0 : 1;
                    pt(tier, s.ax, s.ay); pt(tier, s.bx, s.by);
                    seg(tier, s.ax, s.ay, s.bx, s.by);
                }
                for (const auto& c : g.circles) {
                    int tier = c.construction ? 0 : 1;
                    pt(tier, c.cx, c.cy);   // centre
                    double dd = std::hypot(wx - c.cx, wy - c.cy);
                    double rx, ry;
                    if (dd > 1e-9) { rx = c.cx + (wx - c.cx) / dd * c.r; ry = c.cy + (wy - c.cy) / dd * c.r; }
                    else           { rx = c.cx + c.r; ry = c.cy; }
                    consider(tier, false, std::fabs(dd - c.r), rx, ry);
                }
                for (const auto& a : g.arcs) {
                    int tier = a.construction ? 0 : 1;
                    pt(tier, a.cx, a.cy); pt(tier, a.ax, a.ay); pt(tier, a.bx, a.by);
                    double sx, sy;
                    if (snapOnArc(wx, wy, a, sx, sy)) {
                        consider(tier, false, std::hypot(wx - sx, wy - sy), sx, sy);
                    }
                }
                for (const auto& e : g.ellipses) {
                    int tier = e.construction ? 0 : 1;
                    pt(tier, e.cx, e.cy);
                    double sx, sy;
                    double d = nearestOnEllipse(wx, wy, e.cx, e.cy, e.rx, e.ry, 0.0, TAU, sx, sy);
                    consider(tier, false, d, sx, sy);
                }
                for (const auto& e : g.ellipseArcs) {
                    int tier = e.construction ? 0 : 1;
                    double span = ellipseArcSpan(e);
                    double te = e.a0 + span;
                    pt(tier, e.cx, e.cy);
                    pt(tier, e.cx + e.rx * std::cos(e.a0), e.cy + e.ry * std::sin(e.a0));
                    pt(tier, e.cx + e.rx * std::cos(te),    e.cy + e.ry * std::sin(te));
                    double sx, sy;
                    double d = nearestOnEllipse(wx, wy, e.cx, e.cy, e.rx, e.ry, e.a0, span, sx, sy);
                    consider(tier, false, d, sx, sy);
                }
            }

            // Invisible axis features (tier 2): origin point + the two axis lines.
            consider(2, true,  std::hypot(wx, wy), 0.0, 0.0);   // origin
            consider(2, false, std::fabs(wy),      wx,  0.0);   // X axis (y = 0)
            consider(2, false, std::fabs(wx),      0.0, wy);    // Y axis (x = 0)

            if (found) { wx = bx; wy = by; }
        }

        // Draw one reference's curve into the highlight primitive.
        void appendRef(const SegmentRef& ref, Color color) {

            if (!app || !app->activeProject) { return; }
            const SketchGeometry& g = app->activeProject->geometry;

            switch (ref.kind) {
                case SegmentRef::Kind::PolySeg: {
                    if (ref.index >= g.polylines.size()) { return; }
                    const auto& pts = g.polylines[ref.index].points;
                    if (ref.sub + 1 >= pts.size()) { return; }
                    appendSeg(highlight, pts[ref.sub].x, pts[ref.sub].y,
                              pts[ref.sub + 1].x, pts[ref.sub + 1].y, color);
                    break;
                }
                case SegmentRef::Kind::Segment: {
                    if (ref.index >= g.segments.size()) { return; }
                    const auto& s = g.segments[ref.index];
                    appendSeg(highlight, s.ax, s.ay, s.bx, s.by, color);
                    break;
                }
                case SegmentRef::Kind::Circle: {
                    if (ref.index >= g.circles.size()) { return; }
                    const auto& c = g.circles[ref.index];
                    appendArcSpan(highlight, c.cx, c.cy, c.r, 0.0, TAU, color);
                    break;
                }
                case SegmentRef::Kind::Arc: {
                    if (ref.index >= g.arcs.size()) { return; }
                    appendArc(highlight, g.arcs[ref.index], color);
                    break;
                }
                case SegmentRef::Kind::Ellipse: {
                    if (ref.index >= g.ellipses.size()) { return; }
                    appendEllipse(highlight, g.ellipses[ref.index], color);
                    break;
                }
                case SegmentRef::Kind::EllipseArc: {
                    if (ref.index >= g.ellipseArcs.size()) { return; }
                    appendEllipseArc(highlight, g.ellipseArcs[ref.index], color);
                    break;
                }
            }
        }

        void buildHighlight() {

            highlight->lines.clear();

            // Hover: washed orange-white (transparent). Select: solid pastel
            // orange that pops. Slightly thicker than the geometry.
            Color hoverColor { 1.0f, 0.82f, 0.55f, 0.40f };
            Color selectColor{ 1.0f, 0.62f, 0.28f, 1.0f };

            float base = app ? app->lineThickness : 2.0f;
            highlight->strokeWidth = base + 3.0f;

            for (const SegmentRef& ref : selected) { appendRef(ref, selectColor); }

            if (hoverValid && !isSelected(hovered)) { appendRef(hovered, hoverColor); }

            highlight->compute();
        }

        // Selection / editing actions
        //--------------------------------------------------

        void selectWholePolyline(size_t index) {
            if (!app || !app->activeProject) { return; }
            const auto& g = app->activeProject->geometry;
            if (index >= g.polylines.size()) { return; }
            size_t n = g.polylines[index].points.size();
            for (size_t s = 0; s + 1 < n; s++) {
                SegmentRef r{ SegmentRef::Kind::PolySeg, index, s };
                if (!isSelected(r)) { selected.push_back(r); }
            }
        }

        // Normal click replaces the selection; Ctrl appends; clicking empty space
        // clears (unless Ctrl). Double-click grabs the whole polyline.
        void handleSelectClick(Event& e) {

            double wx, wy;
            screenToWorld(e.mouse.pos.x, e.mouse.pos.y, wx, wy);

            SegmentRef ref;
            bool hit = findHit(wx, wy, hitTolerance(), ref);
            bool ctrl = e.keyboard.ctrl;

            if (hit) {
                if (e.mouse.lb.isDoubleClick() && ref.kind == SegmentRef::Kind::PolySeg) {
                    if (!ctrl) { selected.clear(); }
                    selectWholePolyline(ref.index);
                }
                else if (ctrl) {
                    if (!isSelected(ref)) { selected.push_back(ref); }
                }
                else {
                    selected.clear();
                    selected.push_back(ref);
                }
            }
            else if (!ctrl) {
                selected.clear();
            }

            highlightDirty = true;
            refresh(e);
            e.propagate = false;
        }

        // Delete the selected curves. Selecting any segment of a polyline removes
        // the whole polyline (splitting is a later refinement).
        bool deleteSelected() {

            if (!app || !app->activeProject || selected.empty()) { return false; }

            auto& g = app->activeProject->geometry;
            std::vector<size_t> polys, segs, circs, arcs, ells, ellArcs;

            for (const SegmentRef& r : selected) {
                switch (r.kind) {
                    case SegmentRef::Kind::PolySeg:    polys.push_back(r.index);   break;
                    case SegmentRef::Kind::Segment:    segs.push_back(r.index);    break;
                    case SegmentRef::Kind::Circle:     circs.push_back(r.index);   break;
                    case SegmentRef::Kind::Arc:        arcs.push_back(r.index);    break;
                    case SegmentRef::Kind::Ellipse:    ells.push_back(r.index);    break;
                    case SegmentRef::Kind::EllipseArc: ellArcs.push_back(r.index); break;
                }
            }

            auto removeAt = [](auto& vec, std::vector<size_t> idx) {
                std::sort(idx.begin(), idx.end());
                idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
                for (auto it = idx.rbegin(); it != idx.rend(); ++it) {
                    if (*it < vec.size()) { vec.erase(std::next(vec.begin(), *it)); }
                }
            };

            removeAt(g.polylines, polys);
            removeAt(g.segments, segs);
            removeAt(g.circles, circs);
            removeAt(g.arcs, arcs);
            removeAt(g.ellipses, ells);
            removeAt(g.ellipseArcs, ellArcs);

            app->activeProject->dirty = true;
            selected.clear();
            hoverValid = false;
            return true;
        }

        // Toggle the construction flag on each selected primitive (each unique
        // primitive once, so selecting several segments of one polyline doesn't
        // double-toggle it).
        bool toggleSelectedConstruction() {

            if (!app || !app->activeProject || selected.empty()) { return false; }
            auto& g = app->activeProject->geometry;

            std::vector<std::pair<int, size_t>> done;
            auto seen = [&](int k, size_t i) {
                for (const auto& d : done) { if (d.first == k && d.second == i) { return true; } }
                done.push_back({ k, i });
                return false;
            };

            for (const SegmentRef& r : selected) {
                int k = static_cast<int>(r.kind);
                if (seen(k, r.index)) { continue; }
                switch (r.kind) {
                    case SegmentRef::Kind::PolySeg:
                        if (r.index < g.polylines.size()) { g.polylines[r.index].construction = !g.polylines[r.index].construction; }
                        break;
                    case SegmentRef::Kind::Segment:
                        if (r.index < g.segments.size()) { g.segments[r.index].construction = !g.segments[r.index].construction; }
                        break;
                    case SegmentRef::Kind::Circle:
                        if (r.index < g.circles.size()) { g.circles[r.index].construction = !g.circles[r.index].construction; }
                        break;
                    case SegmentRef::Kind::Arc:
                        if (r.index < g.arcs.size()) { g.arcs[r.index].construction = !g.arcs[r.index].construction; }
                        break;
                    case SegmentRef::Kind::Ellipse:
                        if (r.index < g.ellipses.size()) { g.ellipses[r.index].construction = !g.ellipses[r.index].construction; }
                        break;
                    case SegmentRef::Kind::EllipseArc:
                        if (r.index < g.ellipseArcs.size()) { g.ellipseArcs[r.index].construction = !g.ellipseArcs[r.index].construction; }
                        break;
                }
            }

            app->activeProject->dirty = true;
            return true;
        }

        // Frame all geometry to the viewport.
        void zoomToFit() {

            if (!app || !app->activeProject) { return; }
            const auto& g = app->activeProject->geometry;

            double minX = 1e30, minY = 1e30, maxX = -1e30, maxY = -1e30;
            bool any = false;
            auto acc = [&](double x, double y) {
                minX = std::min(minX, x); minY = std::min(minY, y);
                maxX = std::max(maxX, x); maxY = std::max(maxY, y);
                any = true;
            };

            for (const auto& pl : g.polylines) { for (const auto& p : pl.points) { acc(p.x, p.y); } }
            for (const auto& s : g.segments) { acc(s.ax, s.ay); acc(s.bx, s.by); }
            for (const auto& c : g.circles) { acc(c.cx - c.r, c.cy - c.r); acc(c.cx + c.r, c.cy + c.r); }
            for (const auto& a : g.arcs) { double r = std::hypot(a.ax - a.cx, a.ay - a.cy); acc(a.cx - r, a.cy - r); acc(a.cx + r, a.cy + r); }
            for (const auto& e : g.ellipses) { acc(e.cx - e.rx, e.cy - e.ry); acc(e.cx + e.rx, e.cy + e.ry); }
            for (const auto& e : g.ellipseArcs) { acc(e.cx - e.rx, e.cy - e.ry); acc(e.cx + e.rx, e.cy + e.ry); }
            for (const auto& p : g.points) { acc(p.x, p.y); }

            if (!any) { return; }

            double w = maxX - minX, h = maxY - minY;
            double cx = (minX + maxX) * 0.5, cy = (minY + maxY) * 0.5;

            const double margin = 0.9;
            double sx = (w > 1e-9) ? (rect.w * margin / w) : scale;
            double sy = (h > 1e-9) ? (rect.h * margin / h) : scale;
            scale = std::clamp(static_cast<float>(std::min(sx, sy)), 2.0f, 4000.0f);

            // Put the geometry centre at the viewport centre.
            originX = rect.w * 0.5f - static_cast<float>(cx) * scale;
            originY = rect.h * 0.5f + static_cast<float>(cy) * scale;
        }

        void buildGeometry() {

            geometry->lines.clear();

            // Live line thickness from the app-wide setting.
            if (app) { geometry->strokeWidth = app->lineThickness; }

            // The draw roles.
            Color realColor        { 0.95f, 0.95f, 0.97f, 1.0f };   // true geometry (white)
            Color constructionColor{ 0.60f, 0.60f, 0.66f, 0.85f };  // construction (grey)
            Color candidateColor   { 0.85f, 0.85f, 0.88f, 0.5f };   // intent
            Color helperColor      { 1.0f,  1.0f,  1.0f,  0.1f };   // ghost

            // Committed geometry: real = white, construction = grey, per primitive.
            if (app && app->activeProject) {
                appendCommitted(geometry, app->activeProject->geometry, realColor, constructionColor);
            }

            if (Sketch::App::Tool* tool = currentTool()) {

                Sketch::App::SketchPreview preview;
                tool->preview(cursorX, cursorY, preview);

                // The candidate reads grey while drawing construction geometry.
                Color candidate = drawingConstruction ? constructionColor : candidateColor;

                // Draw order: helpers under construction under the candidate, so
                // the intent geometry reads on top of its ghosts.
                appendGeometry(geometry, preview.helper,       helperColor);
                appendGeometry(geometry, preview.construction, constructionColor);
                appendGeometry(geometry, preview.candidate,    candidate);

                // The cursor itself: the point currently being placed (already
                // snapped), shown as a fixed-size dot since the OS cursor is
                // hidden. Brighter than the candidate so it reads as the cursor.
                Color cursorColor = drawingConstruction
                    ? Color{ 0.72f, 0.72f, 0.78f, 0.95f }
                    : Color{ 0.96f, 0.97f, 1.00f, 0.95f };
                Vertex cv(static_cast<float>(cursorX), static_cast<float>(cursorY));
                geometry->lines.push_back({ .points = { cv, cv }, .color = cursorColor, .strokeWidth = 7.0f });
            }

            geometry->compute();
        }

        // World -> pixel transform handed to the geometry primitive. Pan/zoom
        // only updates this; the points themselves never move on the CPU.
        void updateTransform() {
            FastLines::Xform x { scale, -scale, rect.x + originX, rect.y + originY };
            geometry->transform = x;
            highlight->transform = x;   // highlight overdraws the same world space
            // axes keep the identity transform (they're built in pixel space).
        }

        void computePrimitives(Event& e) override {

            syncTool();

            // Center the world origin in the view the first time we have a size.
            if (!initialized && rect.w > 0.0f && rect.h > 0.0f) {
                originX = rect.w * 0.5f;
                originY = rect.h * 0.5f;
                initialized = true;
                geometryDirty = true;
            }

            // A project switch swaps out the whole geometry.
            if (app && app->activeProject != lastProject) {
                lastProject = app->activeProject;
                geometryDirty = true;
            }

            updateTransform();

            // Axes track the viewport, so they rebuild every frame (cheap). The
            // sketch geometry only rebuilds when it actually changes.
            buildAxes();

            if (geometryDirty) {
                buildGeometry();
                geometryDirty = false;
                highlightDirty = true;   // geometry moved -> highlights are stale
            }

            if (highlightDirty) {
                buildHighlight();
                highlightDirty = false;
            }

            Box::computePrimitives(e);
        }

        void draw(Event& e) override {

            Box::draw(e);

            axes->draw();
            geometry->draw();
            highlight->draw();   // hover / selection on top
        }
    };
}
