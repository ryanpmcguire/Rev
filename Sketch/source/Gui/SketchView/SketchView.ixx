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
import Rev.Core.Pos;

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
    using Rev::Core::Pos;
    using Sketch::App::Pos2;
    using Sketch::App::SnapCandidate;

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

        static constexpr float TAU = 6.283185307179586f;

        Sketch::App::AppState* app = nullptr;

        FastLines* axes = nullptr;       // gnomon: world X / Y axes
        FastLines* geometry = nullptr;   // committed geometry + live preview
        FastLines* highlight = nullptr;  // hover/selection overdraw (orange)

        // A stable reference to one selectable entity, by index (not pointer) so a
        // selection survives appending new geometry. Every entity is atomic now, so
        // there is no sub-part addressing.
        struct EntityRef {
            size_t index = 0;
            bool operator==(const EntityRef& o) const { return index == o.index; }
        };

        std::vector<EntityRef> selected;
        EntityRef hovered;
        bool hoverValid = false;
        bool highlightDirty = true;

        // Whether new geometry is being drawn as construction (reference) geometry.
        bool drawingConstruction = false;

        // Drag-to-move state.
        //
        // Selection and movement share the same gesture: a left press over the
        // selection arms a potential move; the selection change itself is deferred
        // to release, and *skipped* if a drag happened in between (so dragging a
        // member of a multi-selection moves the whole set without collapsing it).
        //
        // A "slot" is one mutable point in the geometry (x,y by pointer plus its
        // value at drag start). The move set is the control points of the moved
        // primitives, plus every other point that was coincident with one of them
        // -- so coincident geometry travels along.
        struct MoveSlot { float* x; float* y; float ox; float oy; };
        std::vector<MoveSlot> moveSlots;

        bool selectPending = false;   // a left gesture whose selection commit is deferred to up
        bool moveArmed     = false;   // pressed over the selection: a move is possible
        bool moveDragged   = false;   // the press has since moved past the click threshold

        EntityRef pendingRef;         // the entity under the press (for the deferred commit)
        bool pendingHit    = false;
        bool pendingCtrl   = false;

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
        float cursorX = 0.0f, cursorY = 0.0f;

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

        Vertex worldToScreen(float wx, float wy) const {
            float sx = rect.x + originX + wx * scale;
            float sy = rect.y + originY - wy * scale;
            return Vertex(sx, sy);
        }

        void screenToWorld(float sx, float sy, float& wx, float& wy) const {
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
                        float wx, wy;
                        screenToWorld(e.mouse.pos.x, e.mouse.pos.y, wx, wy);
                        applySnap(wx, wy);          // magnetism: snap the placed point
                        tool->construction = drawingConstruction;
                        tool->click(*app->activeProject, Pos(wx, wy));
                        geometryDirty = true;
                        refresh(e);
                        e.propagate = false;
                    }
                }
                else {
                    armSelectOrMove(e);
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
                    tool->hover(Pos(cursorX, cursorY));
                    geometryDirty = true;
                    if (hoverValid) { hoverValid = false; highlightDirty = true; }
                    refresh(e);
                }
                else if (!e.mouse.lb) {
                    // Selection mode: hover-highlight the nearest curve. (Skipped
                    // while the left button is held -- that gesture is a move.)
                    EntityRef ref;
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

            // Left drag over an armed selection: translate the move set. A small
            // threshold keeps an ordinary click (with hand jitter) from nudging
            // geometry; once the gesture qualifies as a drag it stays a drag.
            if (e.mouse.lb && moveArmed) {
                if (!moveDragged && e.mouse.diff.pythag() > 3.0f) { moveDragged = true; }
                if (moveDragged) {
                    applyMove(e);
                    refresh(e);
                    e.propagate = false;
                    return;
                }
            }

            Box::mouseDrag(e);
        }

        void mouseUp(Event& e) override {

            // Resolve a deferred selection gesture on release: a clean click (no
            // drag) commits the normal select/toggle; a drag leaves the selection
            // untouched (it was just moved).
            if (e.mouse.lb && selectPending && !currentTool()) {

                if (!moveDragged) { commitSelectClick(); }

                selectPending = false;
                moveArmed = false;
                moveDragged = false;
                moveSlots.clear();

                geometryDirty = true;
                highlightDirty = true;
                refresh(e);
                e.propagate = false;
            }

            Box::mouseUp(e);
        }

        void mouseWheel(Event& e) override {

            if (e.mouse.wheel.y == 0) { return Box::mouseWheel(e); }

            // World point currently under the cursor (the invariant).
            float wx, wy;
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
        void appendSeg(FastLines* dst, float ax, float ay, float bx, float by, Color color) {
            dst->lines.push_back({
                .points = {
                    Vertex(static_cast<float>(ax), static_cast<float>(ay)),
                    Vertex(static_cast<float>(bx), static_cast<float>(by))
                },
                .color = color
            });
        }

        void appendSeg(FastLines* dst, Pos a, Pos b, Color color) {
            appendSeg(dst, a.x, a.y, b.x, b.y, color);
        }

        // A point renders as a single, very short, thick line: the round caps turn
        // it into a crisp dot. A per-line strokeWidth override keeps it a constant
        // pixel size regardless of zoom (the length is zero, so only the caps show).
        void appendPoint(FastLines* dst, const Sketch::App::Point2& p, Color color) {
            Vertex v(static_cast<float>(p.p.x), static_cast<float>(p.p.y));
            float sizePx = (app ? app->lineThickness : 2.0f) * 2.5f;
            dst->lines.push_back({ .points = { v, v }, .color = color, .strokeWidth = sizePx });
        }

        // Sample a circular span of `span` radians starting at a0, in world
        // coordinates (the GPU transform maps it to pixels).
        void appendArcSpan(FastLines* dst, Pos c, float r, float a0, float span, Color color) {

            if (r <= 0.0f || std::fabs(span) < 1e-6f) { return; }

            int steps = std::max(2, static_cast<int>(std::ceil(std::fabs(span) / (TAU / 6400.0f))));

            std::vector<Vertex> pts;
            pts.reserve(steps + 1);

            for (int i = 0; i <= steps; i++) {
                float a = a0 + span * (static_cast<float>(i) / steps);
                Pos p = c + Pos::fromAngle(a) * r;
                pts.push_back(Vertex(p.x, p.y));
            }

            dst->lines.push_back({ .points = std::move(pts), .color = color });
        }

        // CCW span A -> B, in [0, TAU). The arc is always swept counterclockwise.
        static float arcSpan(const Sketch::App::Arc2& a) {
            auto norm = [](float x) { while (x < 0.0f) { x += TAU; } while (x >= TAU) { x -= TAU; } return x; };
            float aA = (a.a - a.c).angle();
            float aB = (a.b - a.c).angle();
            return norm(aB - aA);
        }

        void appendArc(FastLines* dst, const Sketch::App::Arc2& a, Color color) {
            float r = (a.a - a.c).pythag();
            if (r <= 0.0f) { return; }
            float aA = (a.a - a.c).angle();
            appendArcSpan(dst, a.c, r, aA, arcSpan(a), color);
        }

        // Sample a parametric span (t0 .. t0+span) of an ellipse C + cos t U + sin t V.
        void appendEllipseSpan(FastLines* dst, Pos c, Pos u, Pos v,
                               float t0, float span, Color color) {
            if ((u.dot(u) < 1e-12f && v.dot(v) < 1e-12f) || std::fabs(span) < 1e-6f) { return; }
            int steps = std::max(2, static_cast<int>(std::ceil(std::fabs(span) / (TAU / 6400.0f))));
            std::vector<Vertex> pts;
            pts.reserve(steps + 1);
            for (int i = 0; i <= steps; i++) {
                float t = t0 + span * (static_cast<float>(i) / steps);
                Pos p = Sketch::App::ellipsePointAt(c, u, v, t);
                pts.push_back(Vertex(p.x, p.y));
            }
            dst->lines.push_back({ .points = std::move(pts), .color = color });
        }

        void appendEllipse(FastLines* dst, const Sketch::App::Ellipse2& e, Color color) {
            appendEllipseSpan(dst, e.c, e.u, e.v, 0.0f, TAU, color);
        }

        // CCW sweep a0 -> a1, in [0, TAU). The arc is always swept by increasing
        // parameter (counterclockwise in the right-handed U,V frame).
        static float ellipseArcSpan(const Sketch::App::EllipseArc2& e) {
            auto norm = [](float x) { while (x < 0.0f) { x += TAU; } while (x >= TAU) { x -= TAU; } return x; };
            return norm(e.a1 - e.a0);
        }

        void appendEllipseArc(FastLines* dst, const Sketch::App::EllipseArc2& e, Color color) {
            appendEllipseSpan(dst, e.c, e.u, e.v, e.a0, ellipseArcSpan(e), color);
        }

        // Draw a single entity: let it tessellate itself into a polyline, then push
        // it as one Line. Point-like entities become a dot.
        void appendEntity(FastLines* dst, const Sketch::App::Stoicheion& e, Color color) {

            std::vector<Pos> pts;
            e.tessellate(pts);
            if (pts.empty()) { return; }

            if (e.isPoint()) {
                appendPoint(dst, Sketch::App::Point2(pts[0]), color);
                return;
            }

            std::vector<Vertex> vs;
            vs.reserve(pts.size());
            for (const Pos& p : pts) { vs.push_back(Vertex(p.x, p.y)); }
            dst->lines.push_back({ .points = std::move(vs), .color = color });
        }

        void appendGeometry(FastLines* dst, const SketchGeometry& g, Color color) {
            for (const auto& e : g.entities) { if (e) { appendEntity(dst, *e, color); } }
        }

        // Render committed geometry, colouring each entity by whether it is
        // construction (grey) or real (white).
        void appendCommitted(FastLines* dst, const SketchGeometry& g, Color real, Color cons) {
            for (const auto& e : g.entities) {
                if (e) { appendEntity(dst, *e, e->construction ? cons : real); }
            }
        }

        // Hit-testing (SDF distance in world space)
        //--------------------------------------------------

        static float distToSegment(float px, float py, float ax, float ay, float bx, float by) {
            float dx = bx - ax, dy = by - ay;
            float len2 = dx * dx + dy * dy;
            float t = (len2 > 1e-9f) ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);
            return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
        }

        static float distToArc(Pos p, const Sketch::App::Arc2& a) {
            float r = (a.a - a.c).pythag();
            if (r <= 0.0f) { return (p - a.a).pythag(); }
            auto norm = [](float x) { while (x < 0.0f) { x += TAU; } while (x >= TAU) { x -= TAU; } return x; };
            float aA = (a.a - a.c).angle();
            float span = arcSpan(a);                       // CCW, [0, TAU)
            float aP = (p - a.c).angle();
            // Is the cursor angle within the CCW swept range?
            bool within = norm(aP - aA) <= span;
            if (within) { return std::fabs((p - a.c).pythag() - r); }
            return std::min((p - a.a).pythag(), (p - a.b).pythag());
        }

        // Nearest point on an elliptical span (no closed form), via coarse
        // sampling. Returns the distance and writes the snap point.
        static float nearestOnEllipse(Pos p, Pos c, Pos u, Pos v,
                                      float t0, float span, Pos& s) {
            if (u.dot(u) < 1e-12f && v.dot(v) < 1e-12f) {
                s = c; return (p - c).pythag();
            }
            const int N = 96;
            float best = 1e30f;
            for (int i = 0; i <= N; i++) {
                float t = t0 + span * (static_cast<float>(i) / N);
                Pos e = Sketch::App::ellipsePointAt(c, u, v, t);
                float d = (p - e).pythag();
                if (d < best) { best = d; s = e; }
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

        // Find the highest-priority selectable entity (or part) within `thresh`.
        // Each entity contributes its anchor points (point-priority) and its body
        // (curve-priority) via virtual calls; scoring is unchanged.
        bool findHit(float wx, float wy, float thresh, EntityRef& out) const {

            if (!app || !app->activeProject) { return false; }
            const SketchGeometry& g = app->activeProject->geometry;

            Pos w(wx, wy);
            int bestScore = -1; float bestDist = 1e30f; bool found = false;

            auto consider = [&](int tier, bool isPoint, float d, EntityRef ref) {
                if (d >= thresh) { return; }
                int score = participantScore(tier, isPoint);
                if (!found || score > bestScore || (score == bestScore && d < bestDist)) {
                    bestScore = score; bestDist = d; out = ref; found = true;
                }
            };

            // Anchor points first, so on an exact tie at an endpoint the point wins
            // over the curve sharing that spot (you can grab the endpoint itself).
            for (size_t i = 0; i < g.entities.size(); i++) {
                const auto& e = *g.entities[i];
                int tier = e.construction ? 0 : 1;
                std::vector<Pos> anc;
                e.anchors(anc);
                for (const Pos& a : anc) { consider(tier, true, (w - a).pythag(), { i }); }
            }
            for (size_t i = 0; i < g.entities.size(); i++) {
                const auto& e = *g.entities[i];
                int tier = e.construction ? 0 : 1;
                consider(tier, false, e.distanceTo(w), { i });
            }
            return found;
        }

        // Pick tolerance in world units (a few pixels, zoom-independent).
        float hitTolerance() const { return 6.0f / scale; }

        bool isSelected(const EntityRef& ref) const {
            for (const EntityRef& s : selected) { if (s == ref) { return true; } }
            return false;
        }

        // Snapping ("magnetism")
        //--------------------------------------------------

        static void closestOnSegment(float px, float py, float ax, float ay,
                                     float bx, float by, float& fx, float& fy) {
            float dx = bx - ax, dy = by - ay;
            float len2 = dx * dx + dy * dy;
            float t = (len2 > 1e-9f) ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);
            fx = ax + t * dx;
            fy = ay + t * dy;
        }

        // Radial projection of (px,py) onto an arc, valid only within its sweep
        // (endpoints are handled as plain snap points elsewhere).
        static bool snapOnArc(Pos p, const Sketch::App::Arc2& a, Pos& s) {
            float r = (a.a - a.c).pythag();
            if (r <= 0.0f) { return false; }
            auto norm = [](float x) { while (x < 0.0f) { x += TAU; } while (x >= TAU) { x -= TAU; } return x; };
            float aA = (a.a - a.c).angle();
            float span = arcSpan(a);
            float aP = (p - a.c).angle();
            bool within = norm(aP - aA) <= span;
            if (!within) { return false; }
            Pos dir = p - a.c;
            float dd = dir.pythag();
            if (dd < 1e-6f) { s = a.a; }
            else            { s = a.c + dir / dd * r; }
            return true;
        }

        // Gather every snap candidate near `mouse`: each nearby entity yields its
        // own (feature points + curve foot) via snapCandidates, plus the *exact*
        // mutual intersections of the nearby entities (and the world axes). Pure
        // analysis throughout -- nothing here is quantised. The list is scored and
        // can be drawn directly (one chip per candidate) in a later pass.
        void collectSnapCandidates(Pos mouse, float thresh, std::vector<SnapCandidate>& out) const {

            using Sketch::App::Stoicheion;
            using Sketch::App::Segment2;
            using Sketch::App::snapScore;

            // Entities near enough to snap to, gathered for intersection too.
            std::vector<const Stoicheion*> parts;
            std::vector<int> partTier;

            if (app && app->activeProject) {
                const SketchGeometry& g = app->activeProject->geometry;
                for (const auto& ep : g.entities) {
                    if (ep->distanceTo(mouse) < thresh) {
                        ep->snapCandidates(mouse, out);
                        parts.push_back(ep.get());
                        partTier.push_back(ep->construction ? 0 : 1);
                    }
                }
            }

            // The world axes participate as tier-2 "invisible" lines: the origin is
            // a point feature, the axis line a curve foot, and each axis a partner
            // for exact intersections with nearby geometry.
            const float AX = 1.0e5f;   // effectively infinite for local crossings
            Segment2 axisX(Pos(-AX, 0.0f), Pos(AX, 0.0f));
            Segment2 axisY(Pos(0.0f, -AX), Pos(0.0f, AX));

            out.push_back({ Pos(0.0f, 0.0f), SnapCandidate::Point, snapScore(SnapCandidate::Point, 2) });
            if (std::fabs(mouse.y) < thresh) {
                out.push_back({ Pos(mouse.x, 0.0f), SnapCandidate::OnCurve, snapScore(SnapCandidate::OnCurve, 2) });
                parts.push_back(&axisX); partTier.push_back(2);
            }
            if (std::fabs(mouse.x) < thresh) {
                out.push_back({ Pos(0.0f, mouse.y), SnapCandidate::OnCurve, snapScore(SnapCandidate::OnCurve, 2) });
                parts.push_back(&axisY); partTier.push_back(2);
            }

            // Exact mutual intersections between distinct participants.
            for (size_t i = 0; i < parts.size(); i++) {
                for (size_t j = i + 1; j < parts.size(); j++) {
                    std::vector<Pos> hits;
                    Sketch::App::intersect(*parts[i], *parts[j], hits);
                    int tier = std::max(partTier[i], partTier[j]);
                    for (const Pos& x : hits) {
                        if ((mouse - x).pythag() < thresh) {
                            out.push_back({ x, SnapCandidate::Intersection, snapScore(SnapCandidate::Intersection, tier) });
                        }
                    }
                }
            }
        }

        // Magnetism for point placement: collect the candidates near the cursor and
        // nudge it onto the best one within reach -- highest score (point >
        // intersection > curve foot), ties broken by distance to the raw cursor.
        void applySnap(float& wx, float& wy) const {

            float thresh = hitTolerance();
            Pos mouse(wx, wy);

            std::vector<SnapCandidate> cands;
            collectSnapCandidates(mouse, thresh, cands);

            int bestScore = -1; float bestDist = 1e30f; Pos best; bool found = false;
            for (const SnapCandidate& c : cands) {
                float d = (mouse - c.pos).pythag();
                if (d >= thresh) { continue; }
                if (!found || c.score > bestScore || (c.score == bestScore && d < bestDist)) {
                    bestScore = c.score; bestDist = d; best = c.pos; found = true;
                }
            }

            if (found) { wx = best.x; wy = best.y; }
        }

        // Draw one reference's curve into the highlight primitive at a given stroke
        // width (per-line override, so hover and select can differ).
        void appendRef(const EntityRef& ref, Color color, float width) {

            if (!app || !app->activeProject) { return; }
            const SketchGeometry& g = app->activeProject->geometry;
            if (ref.index >= g.entities.size()) { return; }

            std::vector<Pos> pts;
            g.entities[ref.index]->tessellate(pts);
            if (pts.empty()) { return; }

            if (pts.size() == 1) {
                appendPoint(highlight, Sketch::App::Point2(pts[0]), color);
                return;
            }

            std::vector<Vertex> vs;
            vs.reserve(pts.size());
            for (const Pos& p : pts) { vs.push_back(Vertex(p.x, p.y)); }
            highlight->lines.push_back({ .points = std::move(vs), .color = color, .strokeWidth = width });
        }

        void buildHighlight() {

            highlight->lines.clear();

            float base = app ? app->lineThickness : 2.0f;

            // Hover: strongly orange (nearly the selection colour), drawn at the
            // exact width of the geometry it sits over. Select: solid orange that
            // pops, a few px thicker so it reads as a deliberate selection.
            Color hoverColor { 1.0f, 0.58f, 0.30f, 0.9f };
            Color selectColor{ 1.0f, 0.62f, 0.28f, 1.0f };

            highlight->strokeWidth = base + 3.0f;

            for (const EntityRef& ref : selected) { appendRef(ref, selectColor, base + 3.0f); }

            if (hoverValid && !isSelected(hovered)) { appendRef(hovered, hoverColor, base); }

            highlight->compute();
        }

        // Selection / editing actions
        //--------------------------------------------------

        // Left press in selection mode. Rather than commit a selection change
        // immediately, we *arm* the gesture: capture what was clicked, optionally
        // grab a freshly-clicked curve so a drag has something to move, and build
        // the move set. The selection itself is committed on release (and only if
        // the gesture wasn't a drag) -- see commitSelectClick / mouseUp.
        void armSelectOrMove(Event& e) {

            float wx, wy;
            screenToWorld(e.mouse.pos.x, e.mouse.pos.y, wx, wy);

            EntityRef ref;
            bool hit  = findHit(wx, wy, hitTolerance(), ref);
            bool ctrl = e.keyboard.ctrl;

            pendingRef    = ref;
            pendingHit    = hit;
            pendingCtrl   = ctrl;
            selectPending = true;
            moveArmed     = false;
            moveDragged   = false;
            moveSlots.clear();

            if (hit) {
                // Plain-clicking an unselected curve grabs it up front, so a drag
                // has a target and the highlight follows the move. Pressing on an
                // already-selected curve (or any Ctrl press) leaves the selection
                // alone until release, so a drag won't collapse or toggle it.
                if (!ctrl && !isSelected(ref)) {
                    selected.clear();
                    selected.push_back(ref);
                }

                std::vector<EntityRef> moveSet = selected;
                if (!isSelected(ref)) { moveSet.push_back(ref); }
                beginMove(moveSet);
                moveArmed = !moveSlots.empty();
            }

            highlightDirty = true;
            refresh(e);
            e.propagate = false;
        }

        // Commit the deferred selection for a clean (non-drag) click. Normal click
        // replaces the selection; Ctrl toggles; clicking empty space clears (unless
        // Ctrl).
        void commitSelectClick() {

            const EntityRef& ref = pendingRef;

            if (pendingHit) {
                if (pendingCtrl) {
                    // Toggle: drop it if already selected, otherwise add.
                    auto it = std::find(selected.begin(), selected.end(), ref);
                    if (it != selected.end()) { selected.erase(it); }
                    else                      { selected.push_back(ref); }
                }
                else {
                    selected.clear();
                    selected.push_back(ref);
                }
            }
            else if (!pendingCtrl) {
                selected.clear();
            }

            highlightDirty = true;
        }

        // Movement
        //--------------------------------------------------

        // Visit every mutable control point in the geometry as (x&, y&).
        template <class F>
        void forEachPointSlot(F f) {
            if (!app || !app->activeProject) { return; }
            auto& g = app->activeProject->geometry;
            for (auto& e : g.entities) {
                std::vector<Pos2*> cps;
                e->controlPoints(cps);
                for (Pos2* p : cps) { f(p->x, p->y); }
            }
        }

        // Build the move set for the given primitives: their own control points
        // ("drivers"), plus every other point coincident with a driver
        // ("followers"), so touching geometry moves along. Captures each point's
        // start value; the slot pointers stay valid for the gesture because a move
        // only assigns values (never inserts/erases).
        void beginMove(const std::vector<EntityRef>& refs) {

            moveSlots.clear();
            if (!app || !app->activeProject) { return; }
            auto& g = app->activeProject->geometry;

            std::vector<std::pair<float*, float*>> drivers;
            auto addSlot = [&](float& x, float& y) {
                for (auto& d : drivers) { if (d.first == &x) { return; } }
                drivers.push_back({ &x, &y });
            };

            // Each selected entity contributes all of its control points as drivers.
            for (const EntityRef& r : refs) {
                if (r.index >= g.entities.size()) { continue; }
                std::vector<Pos2*> cps;
                g.entities[r.index]->controlPoints(cps);
                for (Pos2* p : cps) { addSlot(p->x, p->y); }
            }

            // Followers: any other control point sitting on a driver at drag start.
            const float eps = 1e-4f;
            std::vector<std::pair<float*, float*>> followers;
            forEachPointSlot([&](float& x, float& y) {
                for (auto& d : drivers)   { if (d.first == &x) { return; } }
                for (auto& fl : followers){ if (fl.first == &x) { return; } }
                for (auto& d : drivers) {
                    if (std::fabs(x - *d.first) <= eps && std::fabs(y - *d.second) <= eps) {
                        followers.push_back({ &x, &y });
                        return;
                    }
                }
            });

            for (auto& d : drivers)   { moveSlots.push_back({ d.first,  d.second,  *d.first,  *d.second }); }
            for (auto& fl : followers){ moveSlots.push_back({ fl.first, fl.second, *fl.first, *fl.second }); }
        }

        // Translate the move set by the gesture's total offset (pixels since the
        // press, mapped to world units), applied to each slot's captured start.
        void applyMove(const Event& e) {

            if (!app || !app->activeProject) { return; }

            float dwx =  e.mouse.diff.x / scale;
            float dwy = -e.mouse.diff.y / scale;

            for (auto& s : moveSlots) { *s.x = s.ox + dwx; *s.y = s.oy + dwy; }

            app->activeProject->dirty = true;
            geometryDirty  = true;
            highlightDirty = true;
        }

        // Delete the selected entities.
        bool deleteSelected() {

            if (!app || !app->activeProject || selected.empty()) { return false; }

            auto& g = app->activeProject->geometry;

            std::vector<size_t> idx;
            for (const EntityRef& r : selected) { idx.push_back(r.index); }

            std::sort(idx.begin(), idx.end());
            idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
            for (auto it = idx.rbegin(); it != idx.rend(); ++it) {
                if (*it < g.entities.size()) { g.entities.erase(std::next(g.entities.begin(), *it)); }
            }

            app->activeProject->dirty = true;
            selected.clear();
            hoverValid = false;
            return true;
        }

        // Toggle the construction flag on each selected entity (once per entity).
        bool toggleSelectedConstruction() {

            if (!app || !app->activeProject || selected.empty()) { return false; }
            auto& g = app->activeProject->geometry;

            std::vector<size_t> done;
            for (const EntityRef& r : selected) {
                if (std::find(done.begin(), done.end(), r.index) != done.end()) { continue; }
                done.push_back(r.index);
                if (r.index < g.entities.size()) {
                    g.entities[r.index]->construction = !g.entities[r.index]->construction;
                }
            }

            app->activeProject->dirty = true;
            return true;
        }

        // Frame all geometry to the viewport.
        void zoomToFit() {

            if (!app || !app->activeProject) { return; }
            const auto& g = app->activeProject->geometry;

            float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
            bool any = false;
            auto acc = [&](float x, float y) {
                minX = std::min(minX, x); minY = std::min(minY, y);
                maxX = std::max(maxX, x); maxY = std::max(maxY, y);
                any = true;
            };

            // Each entity tessellates to its polyline approximation; the sampled
            // points bound it (circles/ellipses included).
            for (const auto& e : g.entities) {
                std::vector<Pos> pts;
                e->tessellate(pts);
                for (const Pos& p : pts) { acc(p.x, p.y); }
            }

            if (!any) { return; }

            float w = maxX - minX, h = maxY - minY;
            float cx = (minX + maxX) * 0.5f, cy = (minY + maxY) * 0.5f;

            const float margin = 0.9f;
            float sx = (w > 1e-6f) ? (rect.w * margin / w) : scale;
            float sy = (h > 1e-6f) ? (rect.h * margin / h) : scale;
            scale = std::clamp(std::min(sx, sy), 2.0f, 4000.0f);

            // Put the geometry centre at the viewport centre.
            originX = rect.w * 0.5f - cx * scale;
            originY = rect.h * 0.5f + cy * scale;
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
                tool->preview(Pos(cursorX, cursorY), preview);

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
