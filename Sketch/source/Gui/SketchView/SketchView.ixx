module;

#include <vector>
#include <cmath>
#include <algorithm>

export module Sketch.Gui.SketchView;

import Rev.Core.Vertex;
import Rev.Core.Color;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;
import Rev.Element.Box;

import Rev.Primitive.Lines;
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

        Lines* axes = nullptr;       // gnomon: world X / Y axes
        Lines* geometry = nullptr;   // committed geometry + live preview

        // Tool state machines (owned; selection mirrors app->activeTool).
        Sketch::App::PointTool  pointTool;
        Sketch::App::LineTool   lineTool;
        Sketch::App::CircleTool circleTool;
        Sketch::App::ArcTool    arcTool;
        SketchTool lastTool = SketchTool::None;

        // CPU camera
        float originX = 0.0f, originY = 0.0f;   // screen px of world (0,0)
        float scale   = 40.0f;                  // pixels per world unit
        bool  initialized = false;

        // Origin captured at the start of a pan (e.mouse.diff is cumulative).
        float panOriginX = 0.0f, panOriginY = 0.0f;

        // Live cursor in world space (drives tool previews).
        double cursorX = 0.0, cursorY = 0.0;

        SketchView(Element* parent, StyleList styles = {}) : Box(parent, styles, "SketchView") {

            app = Sketch::App::AppState::Get(shared->state);

            this->style->size = { .width = Grow(), .height = Grow() };
            this->style->overflow = Overflow::Hide;
            this->style->background.color = rgba(20, 20, 20, 1.0);

            Graphics::Canvas* canvas = shared->canvas;

            axes = new Lines(canvas);
            axes->strokeWidth = 1.0f;
            axes->smoothing = 1.0f;

            geometry = new Lines(canvas);
            geometry->strokeWidth = 2.0f;
            geometry->smoothing = 1.0f;
        }

        ~SketchView() {
            delete axes;
            delete geometry;
        }

        // Tools
        //--------------------------------------------------

        Sketch::App::Tool* currentTool() {
            switch (app ? app->activeTool : SketchTool::None) {
                case SketchTool::Point:  return &pointTool;
                case SketchTool::Line:   return &lineTool;
                case SketchTool::Circle: return &circleTool;
                case SketchTool::Arc:    return &arcTool;
                default:                 return nullptr;
            }
        }

        // Reset the previously-active tool whenever the selection changes, so a
        // half-finished primitive doesn't leak across tool switches.
        void syncTool() {

            SketchTool now = app ? app->activeTool : SketchTool::None;

            if (now == lastTool) { return; }

            switch (lastTool) {
                case SketchTool::Point:  pointTool.reset();  break;
                case SketchTool::Line:   lineTool.reset();   break;
                case SketchTool::Circle: circleTool.reset(); break;
                case SketchTool::Arc:    arcTool.reset();    break;
                default: break;
            }

            lastTool = now;
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

            // Left click drives the active tool's state machine.
            if (e.mouse.lb) {

                syncTool();

                Sketch::App::Tool* tool = currentTool();

                if (tool && app && app->activeProject) {
                    double wx, wy;
                    screenToWorld(e.mouse.pos.x, e.mouse.pos.y, wx, wy);
                    tool->click(*app->activeProject, wx, wy);
                    refresh(e);
                    e.propagate = false;
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

            // Keep the tool preview tracking the cursor while a tool is selected.
            if (Sketch::App::Tool* tool = currentTool()) {
                tool->hover(cursorX, cursorY);
                refresh(e);
            }

            Box::mouseMove(e);
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

            Sketch::App::Tool* tool = currentTool();

            if (tool && app && app->activeProject) {

                if (e.keyboard.enter) {
                    tool->enter(*app->activeProject);
                    refresh(e);
                    e.propagate = false;
                    return;
                }

                if (e.keyboard.escape) {
                    tool->cancel();
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

        // A point renders as a small fixed-size screen-space plus.
        void appendPoint(const Sketch::App::Point2& p, Color color) {

            const float h = 4.0f;
            Vertex s = worldToScreen(p.x, p.y);

            geometry->lines.push_back({
                .points = { Vertex(s.x - h, s.y), Vertex(s.x + h, s.y) },
                .color = color
            });
            geometry->lines.push_back({
                .points = { Vertex(s.x, s.y - h), Vertex(s.x, s.y + h) },
                .color = color
            });
        }

        // Low-level: sample a circular span of `span` radians starting at a0.
        void appendArcSpan(double cx, double cy, double r, double a0, double span, Color color) {

            if (r <= 0.0 || std::fabs(span) < 1e-9) { return; }

            int steps = std::max(2, static_cast<int>(std::ceil(std::fabs(span) / (TAU / 64.0))));

            std::vector<Vertex> pts;
            pts.reserve(steps + 1);

            for (int i = 0; i <= steps; i++) {
                double a = a0 + span * (static_cast<double>(i) / steps);
                pts.push_back(worldToScreen(cx + r * std::cos(a), cy + r * std::sin(a)));
            }

            geometry->lines.push_back({ .points = std::move(pts), .color = color });
        }

        // Render the arc through A, D, B. The direction is whichever way (CCW or
        // CW from A to B) passes through the midpoint D — chirality is implicit
        // in D, not stored separately.
        void appendArc(const Sketch::App::Arc2& a, Color color) {

            double r = std::hypot(a.ax - a.cx, a.ay - a.cy);
            if (r <= 0.0) { return; }

            auto norm = [](double x) {
                while (x < 0.0)  { x += TAU; }
                while (x >= TAU) { x -= TAU; }
                return x;
            };

            double aA = std::atan2(a.ay - a.cy, a.ax - a.cx);
            double aB = std::atan2(a.by - a.cy, a.bx - a.cx);
            double aD = std::atan2(a.dy - a.cy, a.dx - a.cx);

            double spanCCW = norm(aB - aA);   // CCW distance A -> B
            double dD      = norm(aD - aA);   // CCW distance A -> D

            // If D lies on the CCW arc, sweep CCW; otherwise take the CW arc.
            double span = (dD <= spanCCW) ? spanCCW : (spanCCW - TAU);

            appendArcSpan(a.cx, a.cy, r, aA, span, color);
        }

        void appendGeometry(const SketchGeometry& g, Color color) {

            for (const Sketch::App::Segment2& s : g.segments) {
                geometry->lines.push_back({
                    .points = { worldToScreen(s.ax, s.ay), worldToScreen(s.bx, s.by) },
                    .color = color
                });
            }

            for (const Sketch::App::Circle2& c : g.circles) {
                appendArcSpan(c.cx, c.cy, c.r, 0.0, TAU, color);
            }

            for (const Sketch::App::Arc2& a : g.arcs) {
                appendArc(a, color);
            }

            // Points last so their markers sit on top.
            for (const Sketch::App::Point2& p : g.points) {
                appendPoint(p, color);
            }
        }

        void buildGeometry() {

            geometry->lines.clear();

            // The four draw roles.
            Color realColor        { 0.86f, 0.87f, 0.90f, 1.0f };   // true geometry
            Color constructionColor{ 0.40f, 0.85f, 0.95f, 0.6f };   // reserved
            Color candidateColor   { 0.85f, 0.85f, 0.88f, 0.5f };   // intent
            Color helperColor      { 1.0f,  1.0f,  1.0f,  0.1f };   // ghost

            if (app && app->activeProject) {
                appendGeometry(app->activeProject->geometry, realColor);
            }

            if (Sketch::App::Tool* tool = currentTool()) {

                Sketch::App::SketchPreview preview;
                tool->preview(cursorX, cursorY, preview);

                // Draw order: helpers under construction under the candidate, so
                // the intent geometry reads on top of its ghosts.
                appendGeometry(preview.helper,       helperColor);
                appendGeometry(preview.construction, constructionColor);
                appendGeometry(preview.candidate,    candidateColor);
            }

            geometry->compute();
        }

        void computePrimitives(Event& e) override {

            syncTool();

            // Center the world origin in the view the first time we have a size.
            if (!initialized && rect.w > 0.0f && rect.h > 0.0f) {
                originX = rect.w * 0.5f;
                originY = rect.h * 0.5f;
                initialized = true;
            }

            buildAxes();
            buildGeometry();

            Box::computePrimitives(e);
        }

        void draw(Event& e) override {

            Box::draw(e);

            axes->draw();
            geometry->draw();
        }
    };
}
