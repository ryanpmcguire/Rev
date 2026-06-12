module;

#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <iterator>
#include <utility>

#include <dbg.hpp>

export module Sketch.Gui.SketchView;

import Rev.Core.Vertex;
import Rev.Core.Color;
import Rev.Core.Pos;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Event.GestureTracker;
import Rev.Appearance;
import Rev.Element.Box;

import Rev.Primitive.FastLines;
import Rev.Graphics.Canvas;

import Rev.Core.Animator;

import Sketch.App;
import Sketch.App.Project;
import Sketch.App.Tool;
import Sketch.Gui.Theme;
import Sketch.Gui.PreviewBar;

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

    // Keyboard relation commands, matched as key sequences by the GestureTracker.
    // New relations drop in as new bindings ("rd" distance, "rt" tangent, ...).
    enum class SketchCommand {
        RelateCoincident,
        RelateHorizontal,
        RelateVertical,
        RelateLock,
        RelateDistance,
        RelateEqual,
        RelateParallel,
        ToggleConstruction,
        ToggleOpenAir,
        Inset,
    };

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

        // The toolpath playback bar (bottom of the view): scrub / play through
        // the flattened chain list, watching the tool trace it.
        PreviewBar* previewBar = nullptr;

        // A stable reference to one selectable thing, by entity index (not pointer)
        // so a selection survives appending new geometry. `point` addresses one of
        // the entity's defining points (slot into its control points / anchors);
        // `point == -1` means the whole entity.
        struct EntityRef {
            size_t index = 0;
            int point = -1;
            bool operator==(const EntityRef& o) const { return index == o.index && point == o.point; }
        };

        std::vector<EntityRef> selected;
        EntityRef hovered;
        bool hoverValid = false;
        bool highlightDirty = true;

        // Whether new geometry is being drawn as construction (reference) geometry.
        bool drawingConstruction = false;

        // Toolpath sandbox: when on, the active strategy is recomputed from the
        // live geometry every rebuild, so it tracks the sketch as it is dragged.
        // Radius and strategy come from the parameter panel (via AppState).
        bool insetActive = false;

        // Drag-to-move state.
        //
        // Selection and movement share the same gesture: a left press over the
        // selection arms a potential move; the selection change itself is deferred
        // to release, and *skipped* if a drag happened in between (so dragging a
        // member of a multi-selection moves the whole set without collapsing it).
        //
        // A "slot" is one mutable point in the geometry (x,y by pointer, its value
        // at drag start, and the point's stable ref so its motion capacity can be
        // queried). The proposed drag is masked by each point's capacity before it
        // is applied; related points then follow via resolveRelations().
        struct MoveSlot { float* x; float* y; float ox; float oy; Sketch::App::PointRef ref; };
        std::vector<MoveSlot> moveSlots;

        bool selectPending = false;   // a left gesture whose selection commit is deferred to up
        bool moveArmed     = false;   // pressed over the selection: a move is possible
        bool moveDragged   = false;   // the press has since moved past the click threshold

        EntityRef pendingRef;         // the entity under the press (for the deferred commit)
        bool pendingHit    = false;
        bool pendingCtrl   = false;

        // Keyboard relation commands -- every relation is "r"-prefixed; the second
        // key picks the kind (rc coincident, rh/rv parallel-to-axis, rl lock, rd
        // distance). "cc" toggles construction, which is a property, not a relation.
        GestureTracker<SketchCommand> gestures = {
            { "rc", SketchCommand::RelateCoincident },
            { "rh", SketchCommand::RelateHorizontal },
            { "rv", SketchCommand::RelateVertical },
            { "rl", SketchCommand::RelateLock },
            { "rd", SketchCommand::RelateDistance },
            { "re", SketchCommand::RelateEqual },
            { "rp", SketchCommand::RelateParallel },
            { "cc", SketchCommand::ToggleConstruction },
            { "o",  SketchCommand::ToggleOpenAir },        // hovered/selected edge borders open air
            { "i",  SketchCommand::Inset },                // toggle the live toolpath strategy
        };

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
        int lastViewOptions = -1;          // bitmask of the view-select options last built with
        float lastToolRadius = -1.0f;      // parameter-panel values last built with
        int lastIterations = -1;
        std::string lastStrategy;

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

            // Geometry: thin white, tight antialiasing. With the AA band centred on
            // the true edge (see FastLines.frag), strokeWidth now reads at its real
            // width, so a low smoothing keeps the feather crisp instead of blooming.
            geometry = new FastLines(canvas);
            geometry->strokeWidth = 1.0f;
            geometry->smoothing = 0.35f;

            // Highlight: thicker orange overdraw for hover / selection.
            highlight = new FastLines(canvas);
            highlight->smoothing = 0.8f;

            gestures.onGesture = [this](SketchCommand command, Event& e) {
                bool changed = false;
                switch (command) {
                    case SketchCommand::RelateCoincident:   changed = relateCoincident();         break;
                    case SketchCommand::RelateHorizontal:   changed = relateParallel(false);      break;
                    case SketchCommand::RelateVertical:     changed = relateParallel(true);       break;
                    case SketchCommand::RelateLock:         changed = lockSelected();             break;
                    case SketchCommand::RelateDistance:     changed = relateDistance();           break;
                    case SketchCommand::RelateEqual:        changed = relateEqual();              break;
                    case SketchCommand::RelateParallel:     changed = relateParallelSegments();   break;
                    case SketchCommand::ToggleConstruction: changed = toggleConstructionCommand(); break;
                    case SketchCommand::ToggleOpenAir:      changed = toggleOpenAirCommand();     break;
                    case SketchCommand::Inset:              changed = insetCommand();            break;
                }
                if (changed) {
                    geometryDirty = true;
                    highlightDirty = true;
                    refresh(e);
                }
            };

            // Toolpath playback: the bar owns percent + transport; the view maps
            // percent onto the flattened chain list each rebuild.
            previewBar = new PreviewBar(this);

            previewBar->onPercentChanged = [this](Event& e) {
                geometryDirty = true;
                refresh(e);
            };

            previewBar->onAnimateFrame = [this](Rev::Core::AnimationEvent& frame, Event& e) {
                // A full run takes ~12 seconds end to end (speed knob later).
                float step = static_cast<float>(frame.deltaMs) * (100.0f / 12000.0f);
                float next = previewBar->percent + step;
                if (next >= 100.0f) { next = 100.0f; previewBar->pause(e); }
                previewBar->setPercent(next, e);   // triggers onPercentChanged -> rebuild
            };
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
                        Pos placed(wx, wy);

                        size_t before = app->activeProject->geometry.entities.size();
                        tool->construction = drawingConstruction;
                        tool->click(*app->activeProject, placed);

                        // Record intent: any new control point sitting on a pre-existing
                        // one becomes a Coincident relation (connecting to endpoints,
                        // continuing a polyline, snapping onto real geometry).
                        autoRelateCoincidences(before);

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
            // untouched (it was just moved). NB: the left button is already released
            // by the time mouseUp fires (so we don't gate on e.mouse.lb) -- a pending
            // selection only ever comes from a prior left press.
            if (selectPending && !currentTool()) {

                if (!moveDragged) { commitSelectClick(); }

                // On release, settle the drag crisply: hold the released points where
                // they ended up and run a tighter relaxation so the soft mid-drag
                // motion converges onto the relations as cleanly as it can.
                if (moveDragged && app && app->activeProject) {
                    std::vector<Sketch::App::DragPoint> held;
                    for (auto& s : moveSlots) { held.push_back({ s.ref, Pos(*s.x, *s.y) }); }
                    app->activeProject->geometry.relaxDrag(held, 12, 1.0f);
                    app->activeProject->geometry.normalize();
                }

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

            // Command sequences ("rc" coincident, "cl" lock, "cc" construction).
            // The tracker consumes an in-progress sequence (e.g. a lone "r"/"c"
            // prefix) and fires onGesture on a match; everything else (f, Del, Esc,
            // arrows) passes through to the handlers below.
            if (gestures.track(e)) {
                e.propagate = false;
                return;
            }

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

            // (Construction toggle is now the "cc" gesture; lock is "cl".)

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
        void appendPoint(FastLines* dst, const Sketch::App::Point2& p, Color color, float scale = 2.5f) {
            Vertex v(static_cast<float>(p.p.x), static_cast<float>(p.p.y));
            float sizePx = (app ? app->lineThickness : 2.0f) * scale;
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

        void appendArc(FastLines* dst, const Sketch::App::Arc2& a, Color color) {
            float r = (a.a - a.c).pythag();
            if (r <= 0.0f) { return; }
            float a0, sweep; a.range(a0, sweep);   // the half through D
            appendArcSpan(dst, a.c, r, a0, sweep, color);
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
        // `arrow = true` additionally draws a little arrowhead at the entity's
        // travel midpoint, pointing the way the curve is walked -- so every edge of
        // a chain display wears its direction on its sleeve.
        void appendEntity(FastLines* dst, const Sketch::App::Stoicheion& e, Color color, bool arrow = false) {

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

            if (arrow && pts.size() >= 2) {
                // Arrow at the END of travel. Arc tessellation runs over the CCW
                // span regardless of travel chirality -- ask the true travel
                // tangent (and true endpoint) for circular edges.
                using Sketch::App::SKind;
                bool circular = (e.type() == SKind::Arc || e.type() == SKind::Circle);
                Pos tip = circular ? Sketch::App::Chain::eEnd(e) : pts.back();
                Pos t = circular ? Sketch::App::Chain::travelDirAt(e, tip)
                                 : (pts.back() - pts[pts.size() - 2]);
                float len = t.pythag();
                if (len > 1e-9f) {
                    t = t / len;
                    Pos left(-t.y, t.x);
                    float s = 9.0f / scale;                      // ~9 px, zoom-invariant
                    Vertex vt(tip.x, tip.y);
                    Pos b1 = tip - t * s + left * (s * 0.45f);
                    Pos b2 = tip - t * s - left * (s * 0.45f);
                    dst->lines.push_back({ .points = { Vertex(b1.x, b1.y), vt }, .color = color });
                    dst->lines.push_back({ .points = { Vertex(b2.x, b2.y), vt }, .color = color });
                }
            }
        }

        // Draw an edge split at `cut` arc length along its travel: the part
        // already travelled in `done`, the rest in `pending`. Returns the cut
        // point (the playhead).
        Pos appendEntitySplit(FastLines* dst, const Sketch::App::Stoicheion& e, float cut,
                              Color done, Color pending) {

            std::vector<Pos> pts;
            e.tessellate(pts);
            if (pts.size() < 2) { return pts.empty() ? Pos() : pts[0]; }

            // Tessellation runs over an arc's CCW span regardless of travel
            // chirality -- orient it to TRUE travel before cutting, or the
            // playhead walks the arc backwards.
            Pos start = Sketch::App::Chain::eStart(e);
            if ((pts.front() - start).pythag() > (pts.back() - start).pythag()) {
                std::reverse(pts.begin(), pts.end());
            }

            std::vector<Vertex> a, b;
            Pos playhead = pts.front();
            float acc = 0.0f;
            bool past = false;

            a.push_back(Vertex(pts[0].x, pts[0].y));
            for (size_t i = 1; i < pts.size(); i++) {
                float len = (pts[i] - pts[i - 1]).pythag();
                if (!past && acc + len >= cut && len > 1e-9f) {
                    float t = (cut - acc) / len;
                    Pos mid = pts[i - 1] + (pts[i] - pts[i - 1]) * t;
                    a.push_back(Vertex(mid.x, mid.y));
                    b.push_back(Vertex(mid.x, mid.y));
                    b.push_back(Vertex(pts[i].x, pts[i].y));
                    playhead = mid;
                    past = true;
                }
                else if (past) { b.push_back(Vertex(pts[i].x, pts[i].y)); }
                else { a.push_back(Vertex(pts[i].x, pts[i].y)); }
                acc += len;
            }
            if (!past) { playhead = pts.back(); }

            if (a.size() >= 2) { dst->lines.push_back({ .points = std::move(a), .color = done }); }
            if (b.size() >= 2) { dst->lines.push_back({ .points = std::move(b), .color = pending }); }
            return playhead;
        }

        void appendGeometry(FastLines* dst, const SketchGeometry& g, Color color) {
            for (const auto& e : g.entities) { if (e) { appendEntity(dst, *e, color); } }
        }

        // Render committed geometry, colouring each entity: construction = grey,
        // fully-constrained ("solved") = royal blue, otherwise = white. Every
        // endpoint is dotted (clearly larger than the stroke) so the defining points
        // read at a glance; a solved point dots royal blue, so you can see exactly
        // which ends are pinned even on an under-constrained line. Locked datums
        // (origin / axes) aren't drawn -- the crisp gnomon stands in.
        void appendCommitted(FastLines* dst, const SketchGeometry& g,
                             Color real, Color cons, Color solvedColor) {

            constexpr float EndpointScale = 4.5f;   // dot size relative to stroke width

            Color airColor { 0.45f, 0.78f, 1.00f, 0.95f };   // open-air edges: sky blue

            for (const auto& e : g.entities) {
                if (!e || e->locked) { continue; }

                Color c = e->construction ? cons
                        : e->openAir ? airColor
                        : (g.solved(*e) ? solvedColor : real);
                appendEntity(dst, *e, c);

                if (e->isPoint()) { continue; }   // a point entity already renders as its dot

                std::vector<Pos> anc;
                e->anchors(anc);
                int marker = e->chiralitySlot();   // don't dot the chirality marker
                for (size_t s = 0; s < anc.size(); s++) {
                    if (static_cast<int>(s) == marker) { continue; }
                    bool ptSolved = g.pointSolved({ e->id, static_cast<int>(s) });
                    appendPoint(dst, Sketch::App::Point2(anc[s]), ptSolved ? solvedColor : c, EndpointScale);
                }
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
            float a0, sweep; a.range(a0, sweep);           // the half through D
            float aP = (p - a.c).angle();
            bool within = norm(aP - a0) <= sweep;
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

        // Selection priority tier (distinct from the *snapping* tier in tierOf,
        // where axes lead). For picking, real and construction geometry both outrank
        // the datum axes -- you almost never mean to grab a giant axis when real
        // geometry is under the cursor -- so axes sit lowest: real 2, construction 1,
        // axis/datum 0. Points (and line endpoints) outrank curves within a tier.
        static int selectTier(const Sketch::App::Stoicheion& e) {
            if (e.locked) { return 0; }
            return e.construction ? 1 : 2;
        }

        // Higher wins; we pick the highest-scoring participant, breaking ties by
        // distance -- not simply the nearest.
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

            // Defining points first (so an endpoint wins over the curve sharing its
            // spot). Each anchor is a control-point slot, in the same order as
            // controlPoints(), so the ref addresses exactly that point.
            for (size_t i = 0; i < g.entities.size(); i++) {
                const auto& e = *g.entities[i];
                int tier = selectTier(e);
                int marker = e.chiralitySlot();   // the chirality marker isn't selectable
                std::vector<Pos> anc;
                e.anchors(anc);
                for (size_t s = 0; s < anc.size(); s++) {
                    if (static_cast<int>(s) == marker) { continue; }
                    consider(tier, true, (w - anc[s]).pythag(), { i, static_cast<int>(s) });
                }
            }
            for (size_t i = 0; i < g.entities.size(); i++) {
                const auto& e = *g.entities[i];
                int tier = selectTier(e);
                consider(tier, false, e.distanceTo(w), { i, -1 });
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
            float a0, sweep; a.range(a0, sweep);           // the half through D
            float aP = (p - a.c).angle();
            bool within = norm(aP - a0) <= sweep;
            if (!within) { return false; }
            Pos dir = p - a.c;
            float dd = dir.pythag();
            if (dd < 1e-6f) { s = a.a; }
            else            { s = a.c + dir / dd * r; }
            return true;
        }

        // Tier for scoring: locked datums (origin / axes) are tier-2 authority, real
        // geometry tier 1, construction tier 0.
        static int tierOf(const Sketch::App::Stoicheion& e) {
            return e.locked ? 2 : (e.construction ? 0 : 1);
        }

        // Gather every snap candidate near `mouse`: each nearby entity yields its
        // own (feature points + curve foot) via snapCandidates, plus the *exact*
        // mutual intersections of the nearby entities. The origin and axes are just
        // ordinary (locked) entities now, so they fall out for free. Pure analysis
        // throughout -- nothing here is quantised.
        void collectSnapCandidates(Pos mouse, float thresh, std::vector<SnapCandidate>& out) const {

            using Sketch::App::Stoicheion;

            // Entities near enough to snap to, gathered for intersection too.
            std::vector<const Stoicheion*> parts;
            std::vector<int> partTier;

            if (app && app->activeProject) {
                const SketchGeometry& g = app->activeProject->geometry;
                for (const auto& ep : g.entities) {
                    if (ep->distanceTo(mouse) < thresh) {
                        ep->snapCandidates(mouse, out);
                        parts.push_back(ep.get());
                        partTier.push_back(tierOf(*ep));
                    }
                }
            }

            // Exact mutual intersections between distinct participants.
            for (size_t i = 0; i < parts.size(); i++) {
                for (size_t j = i + 1; j < parts.size(); j++) {
                    std::vector<Pos> hits;
                    Sketch::App::intersect(*parts[i], *parts[j], hits);
                    int tier = std::max(partTier[i], partTier[j]);
                    for (const Pos& x : hits) {
                        if ((mouse - x).pythag() < thresh) {
                            out.push_back({ x, SnapCandidate::Intersection,
                                            Sketch::App::snapScore(SnapCandidate::Intersection, tier) });
                        }
                    }
                }
            }
        }

        // Find an existing control point lying (essentially) at `p`, searching only
        // entities below index `limit` (so freshly-added geometry is excluded).
        bool controlPointAt(Pos p, size_t limit, Sketch::App::PointRef& out) const {
            const SketchGeometry& g = app->activeProject->geometry;
            for (size_t i = 0; i < limit && i < g.entities.size(); i++) {
                if (!g.entities[i]) { continue; }
                std::vector<Pos> anc;
                g.entities[i]->anchors(anc);
                for (size_t s = 0; s < anc.size(); s++) {
                    if ((anc[s] - p).pythag() < 1e-3f) {
                        out = { g.entities[i]->id, static_cast<int>(s) };
                        return true;
                    }
                }
            }
            return false;
        }

        // After a tool click added entities (those from index `before` on), tie each
        // of their control points that lands on a pre-existing control point to it
        // via a Coincident relation -- making "I connected here" explicit in the
        // graph (endpoints, polyline continuation, snapping onto real geometry).
        void autoRelateCoincidences(size_t before) {
            if (!app || !app->activeProject) { return; }
            auto& g = app->activeProject->geometry;
            bool added = false;
            for (size_t i = before; i < g.entities.size(); i++) {
                if (!g.entities[i]) { continue; }
                std::vector<Pos> anc;
                g.entities[i]->anchors(anc);
                for (size_t s = 0; s < anc.size(); s++) {
                    Sketch::App::PointRef target;
                    if (controlPointAt(anc[s], before, target)) {        // existing point here
                        Sketch::App::PointRef np{ g.entities[i]->id, static_cast<int>(s) };
                        app->activeProject->addRelation(
                            std::make_unique<Sketch::App::Coincident>(target, np));  // new follows existing
                        added = true;
                    }
                }
            }
            if (added) { g.resolveRelations(); }
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
            const Sketch::App::Stoicheion& e = *g.entities[ref.index];

            // A point ref highlights just that defining point as a dot.
            if (ref.point >= 0) {
                std::vector<Pos> anc;
                e.anchors(anc);
                if (ref.point < static_cast<int>(anc.size())) {
                    appendPoint(highlight, Sketch::App::Point2(anc[ref.point]), color);
                }
                return;
            }

            std::vector<Pos> pts;
            e.tessellate(pts);
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

        // Build the move set. Each selected point (a point ref → that one control
        // point; a whole-entity ref → all of its control points) is expanded to its
        // whole *coincident cluster*, so a free cluster drags as one. Capacity then
        // decides: a cluster anchored to a solved point is Locked and won't move; a
        // free cluster moves together. Locked datums are never grabbed.
        void beginMove(const std::vector<EntityRef>& refs) {

            moveSlots.clear();
            if (!app || !app->activeProject) { return; }
            auto& g = app->activeProject->geometry;

            // 1. The seed points from the selection.
            std::vector<Sketch::App::PointRef> seeds;
            auto seed = [&](Sketch::App::PointRef pr) {
                for (auto& s : seeds) { if (s == pr) { return; } }
                seeds.push_back(pr);
            };
            for (const EntityRef& r : refs) {
                if (r.index >= g.entities.size()) { continue; }
                Sketch::App::Stoicheion& e = *g.entities[r.index];
                if (e.locked) { continue; }
                std::vector<Pos2*> cps;
                e.controlPoints(cps);
                if (r.point < 0) {
                    for (size_t s = 0; s < cps.size(); s++) { seed({ e.id, static_cast<int>(s) }); }
                }
                else if (r.point < static_cast<int>(cps.size())) {
                    seed({ e.id, r.point });
                }
            }

            // 2. Expand each seed to its coincident cluster.
            std::vector<Sketch::App::PointRef> all;
            auto addRef = [&](Sketch::App::PointRef pr) {
                for (auto& a : all) { if (a == pr) { return; } }
                all.push_back(pr);
            };
            for (const auto& s : seeds) {
                std::vector<Sketch::App::PointRef> cluster;
                g.coincidentClosure(s, cluster);
                for (const auto& c : cluster) { addRef(c); }
            }

            // 3. Resolve each ref to its mutable point and add a slot (datums skipped).
            for (const auto& pr : all) {
                Sketch::App::Stoicheion* e = g.byId(pr.entity);
                if (!e || e->locked) { continue; }
                std::vector<Pos2*> cps;
                e->controlPoints(cps);
                if (pr.slot < 0 || pr.slot >= static_cast<int>(cps.size())) { continue; }
                Pos2* p = cps[pr.slot];
                bool dup = false;
                for (auto& sl : moveSlots) { if (sl.x == &p->x) { dup = true; break; } }
                if (!dup) { moveSlots.push_back({ &p->x, &p->y, p->x, p->y, pr }); }
            }
        }

        // Apply the proposed drag (pixels since the press, mapped to world). Each held
        // point keeps the part of the proposal its *own* capacity allows -- so a point
        // bound to a line slides along it and a grounded point stays put. Those held
        // points then become the anchors of the relaxation solver, which distributes
        // the motion through the relation graph: distance-constrained neighbours get
        // dragged along, and a free-floating cluster (anchored to nothing) moves
        // bodily, root and all.
        void applyMove(const Event& e) {

            if (!app || !app->activeProject) { return; }
            auto& g = app->activeProject->geometry;

            Pos proposal(e.mouse.diff.x / scale, -e.mouse.diff.y / scale);

            std::vector<Sketch::App::DragPoint> held;
            for (auto& s : moveSlots) {
                Pos allowed = g.capacityOf(s.ref).mask(proposal);
                held.push_back({ s.ref, Pos(s.ox, s.oy) + allowed });
            }
            // Fully settle the graph each frame (held points pinned at the cursor), so
            // what we display is always resolved -- never a half-converged structure
            // that only snaps back into place on the next drag.
            g.relaxDrag(held, 64, 1.0f);
            g.normalize();   // re-seat chirality markers to ride along with the move

            app->activeProject->dirty = true;
            geometryDirty  = true;
            highlightDirty = true;
        }

        // Delete the selected entities.
        bool deleteSelected() {

            if (!app || !app->activeProject || selected.empty()) { return false; }

            auto& g = app->activeProject->geometry;

            std::vector<size_t> idx;
            for (const EntityRef& r : selected) {
                if (r.index < g.entities.size() && !g.entities[r.index]->locked) { idx.push_back(r.index); }
            }

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

        // Propose a relation to the active geometry: it settles the whole graph and
        // keeps the relation only if it can coexist with the senior relations already
        // in force (otherwise the junior newcomer is withdrawn). Reports a rejection.
        bool propose(std::unique_ptr<Sketch::App::Relation> r) {
            if (!app || !app->activeProject) { return false; }
            bool ok = app->activeProject->geometry.proposeRelation(std::move(r));
            app->activeProject->dirty = true;
            if (!ok) { dbg("[relation] rejected: conflicts with a more senior relation"); }
            return ok;
        }

        // "rc" -- relate the two selected defining points as coincident. Symmetric:
        // neither drives, the solver pulls them together (a grounded point wins).
        bool relateCoincident() {

            if (!app || !app->activeProject) { return false; }
            auto& g = app->activeProject->geometry;

            std::vector<EntityRef> pts;
            for (const EntityRef& r : selected) {
                if (r.point >= 0 && r.index < g.entities.size()) { pts.push_back(r); }
            }
            if (pts.size() != 2) { return false; }

            auto ref = [&](const EntityRef& r) {
                return Sketch::App::PointRef{ g.entities[r.index]->id, r.point };
            };

            propose(std::make_unique<Sketch::App::Coincident>(ref(pts[0]), ref(pts[1])));

            selected.clear();
            hoverValid = false;
            return true;
        }

        // "cl" -- lock each selected point in place with a Lock relation, giving it
        // zero motion capacity. (Datums are already locked.)
        bool lockSelected() {

            if (!app || !app->activeProject) { return false; }
            auto& g = app->activeProject->geometry;

            int locked = 0;
            for (const EntityRef& r : selected) {
                if (r.point < 0 || r.index >= g.entities.size()) { continue; }
                Sketch::App::Stoicheion& e = *g.entities[r.index];
                if (e.locked) { continue; }
                std::vector<Pos2*> cps;
                e.controlPoints(cps);
                if (r.point >= static_cast<int>(cps.size())) { continue; }
                Pos at = *cps[r.point];
                app->activeProject->addRelation(
                    std::make_unique<Sketch::App::Lock>(Sketch::App::PointRef{ e.id, r.point }, at));
                locked++;
            }

            if (locked > 0) { g.resolveRelations(); selected.clear(); hoverValid = false; }
            return locked > 0;
        }

        // Resolve the current selection to a pair of control points: either two
        // explicitly selected points, or the first two anchors of a selected entity
        // (e.g. a segment's endpoints). The more-solved point is placed first so it
        // becomes the master that the other follows. Returns false if no pair exists.
        bool twoRelationPoints(std::vector<Sketch::App::PointRef>& pts) {

            if (!app || !app->activeProject) { return false; }
            auto& g = app->activeProject->geometry;

            pts.clear();
            std::vector<EntityRef> sel;
            for (const EntityRef& r : selected) {
                if (r.point >= 0 && r.index < g.entities.size()) { sel.push_back(r); }
            }
            if (sel.size() == 2) {
                pts = { { g.entities[sel[0].index]->id, sel[0].point },
                        { g.entities[sel[1].index]->id, sel[1].point } };
            }
            else {
                for (const EntityRef& r : selected) {
                    if (r.index >= g.entities.size()) { continue; }
                    Sketch::App::Stoicheion& e = *g.entities[r.index];
                    std::vector<Pos> anc;
                    e.anchors(anc);
                    if (anc.size() >= 2) { pts = { { e.id, 0 }, { e.id, 1 } }; break; }
                }
            }
            if (pts.size() != 2) { return false; }

            // Authority: the more-solved point is the master that the other follows.
            if (g.pointSolved(pts[1]) && !g.pointSolved(pts[0])) { std::swap(pts[0], pts[1]); }
            return true;
        }

        // "rd" -- relate distance: a dimension is just a Distance relation pinning the
        // separation between two points (or a segment's endpoints) to its current
        // length. The more-solved point becomes the authority.
        bool relateDistance() {

            std::vector<Sketch::App::PointRef> pts;
            if (!twoRelationPoints(pts)) { return false; }
            auto& g = app->activeProject->geometry;

            float d = (g.posOf(pts[1]) - g.posOf(pts[0])).pythag();
            propose(std::make_unique<Sketch::App::Distance>(pts[0], pts[1], d));

            selected.clear();
            hoverValid = false;
            return true;
        }

        // "rh" / "rv" -- relate horizontal / vertical: make the segment parallel to a
        // world axis via a Parallel relation that references the axis' locked
        // endpoints as its direction. `vertical` picks the Y axis, else the X axis.
        bool relateParallel(bool vertical) {

            std::vector<Sketch::App::PointRef> pts;
            if (!twoRelationPoints(pts)) { return false; }
            auto& g = app->activeProject->geometry;

            Sketch::App::Id ax = g.axisId(vertical);
            if (ax == 0) { return false; }
            Sketch::App::PointRef refA{ ax, 0 }, refB{ ax, 1 };

            propose(std::make_unique<Sketch::App::Parallel>(pts[0], pts[1], refA, refB));

            selected.clear();
            hoverValid = false;
            return true;
        }

        // Resolve the selection to two distinct segments (entities with >= 2 control
        // points). The more-solved one is returned as the reference (authority); the
        // other as the follower. Returns false if there aren't two.
        bool twoRelationSegments(Sketch::App::Id& refId, Sketch::App::Id& folId) {

            if (!app || !app->activeProject) { return false; }
            auto& g = app->activeProject->geometry;

            std::vector<Sketch::App::Id> segs;
            for (const EntityRef& r : selected) {
                if (r.index >= g.entities.size()) { continue; }
                std::vector<Pos> anc;
                g.entities[r.index]->anchors(anc);
                if (anc.size() < 2) { continue; }
                Sketch::App::Id id = g.entities[r.index]->id;
                bool dup = false;
                for (Sketch::App::Id s : segs) { if (s == id) { dup = true; break; } }
                if (!dup) { segs.push_back(id); }
                if (segs.size() == 2) { break; }
            }
            if (segs.size() != 2) { return false; }

            refId = segs[0]; folId = segs[1];
            Sketch::App::Stoicheion* ref = g.byId(refId);
            Sketch::App::Stoicheion* fol = g.byId(folId);
            if (ref && fol && g.solved(*fol) && !g.solved(*ref)) { std::swap(refId, folId); }
            return true;
        }

        // "re" -- relate equal: hold two selected segments at the same length. The
        // more-solved segment is the reference; the other follows it.
        bool relateEqual() {

            Sketch::App::Id refId, folId;
            if (!twoRelationSegments(refId, folId)) { return false; }

            propose(std::make_unique<Sketch::App::Equal>(
                Sketch::App::PointRef{ folId, 0 }, Sketch::App::PointRef{ folId, 1 },
                Sketch::App::PointRef{ refId, 0 }, Sketch::App::PointRef{ refId, 1 }));

            selected.clear();
            hoverValid = false;
            return true;
        }

        // "rp" -- relate parallel: make one selected segment parallel to another, the
        // reference segment's endpoints standing in for the direction (exactly as an
        // axis does for horizontal/vertical). The more-solved segment is the reference.
        bool relateParallelSegments() {

            Sketch::App::Id refId, folId;
            if (!twoRelationSegments(refId, folId)) { return false; }

            propose(std::make_unique<Sketch::App::Parallel>(
                Sketch::App::PointRef{ folId, 0 }, Sketch::App::PointRef{ folId, 1 },
                Sketch::App::PointRef{ refId, 0 }, Sketch::App::PointRef{ refId, 1 }));

            selected.clear();
            hoverValid = false;
            return true;
        }

        // "cc" -- toggle construction: flip the selected entities, or (with nothing
        // selected) flip the drawing mode so future geometry inherits it.
        bool toggleConstructionCommand() {
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
            return true;
        }

        // "i" -- toggle the live inset. While on, the offset profile is regenerated
        // from the current geometry on every rebuild (see buildGeometry), so it
        // tracks the sketch in real time as it is dragged. Off clears it.
        bool insetCommand() {
            if (!app || !app->activeProject) { return false; }
            insetActive = !insetActive;
            if (!insetActive) {
                app->activeProject->offsetLayers.clear();
                app->activeProject->offsetChains.clear();
                app->activeProject->sourceChains.clear();
                app->activeProject->crossingFragments.clear();
                app->activeProject->profiles.clear();
            }
            return true;
        }

        // "o" -- mark the hovered (or selected) edge as bordering OPEN AIR: free
        // space the tool may run off into. The profile strategy pre-pushes such
        // edges outward so corners against the open region get fully covered.
        bool toggleOpenAirCommand() {
            if (!app || !app->activeProject) { return false; }
            auto& g = app->activeProject->geometry;

            if (hoverValid && hovered.index < g.entities.size()) {
                auto& e = g.entities[hovered.index];
                if (e && !e->locked) { e->openAir = !e->openAir; app->activeProject->dirty = true; return true; }
                return false;
            }

            bool changed = false;
            std::vector<size_t> done;
            for (const EntityRef& r : selected) {
                if (std::find(done.begin(), done.end(), r.index) != done.end()) { continue; }
                done.push_back(r.index);
                if (r.index < g.entities.size() && g.entities[r.index] && !g.entities[r.index]->locked) {
                    g.entities[r.index]->openAir = !g.entities[r.index]->openAir;
                    changed = true;
                }
            }
            if (changed) { app->activeProject->dirty = true; }
            return changed;
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
            // points bound it. Locked datums (the huge axes) are excluded.
            for (const auto& e : g.entities) {
                if (e->locked) { continue; }
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
            Color realColor        { 0.95f, 0.95f, 0.97f, 1.0f };   // under-constrained (white)
            Color solvedColor      { 0.25f, 0.41f, 0.88f, 1.0f };   // solved (royal blue)
            Color constructionColor{ 0.60f, 0.60f, 0.66f, 0.85f };  // construction (grey)
            Color candidateColor   { 0.85f, 0.85f, 0.88f, 0.5f };   // intent
            Color helperColor      { 1.0f,  1.0f,  1.0f,  0.1f };   // ghost

            // Committed geometry: white = under-constrained, green = solved,
            // grey = construction.
            if (app && app->activeProject) {
                appendCommitted(geometry, app->activeProject->geometry,
                                realColor, constructionColor, solvedColor);

                // Live toolpathing (the "i" toggle): the active strategy is re-run
                // on the current geometry each rebuild, so it follows the sketch as
                // it is dragged. The view-select options are mirrored onto the
                // project first; radius and strategy come from the parameter panel.
                if (insetActive) {
                    app->activeProject->viewValid     = app->viewValid;
                    app->activeProject->viewWinding   = app->viewWinding;
                    app->activeProject->viewDiscarded = app->viewDiscarded;
                    app->activeProject->viewToolpath  = app->viewToolpath;
                    app->activeProject->reverseToolpath = app->toolReverse;
                    app->activeProject->climbMilling  = app->climbMilling;
                    app->activeProject->iterations    = app->iterations;
                    app->activeProject->runStrategy(app->strategy, app->toolRadius);
                }

                // Offset layers, styled blindly by each stoicheion's own group label
                // (the view maps label -> style, it decides nothing).
                //   chirality/* (signed-area method): ABSOLUTE chirality -- every CW
                //     loop blue, every CCW loop red, regardless of source.
                //   crossing/N (crossing method): the accumulated crossing number --
                //     0 (settled) green, positive reds, negative blues, dimming as
                //     the count deepens.
                // The "intersections" group renders as yellow dots, drawn second so
                // it sits on top.
                // Debug anatomy renders at 2/3 opacity; the valid chains at full.
                constexpr float DebugA = 0.66f;
                Color cwColor    { 0.30f, 0.50f, 1.00f, DebugA };   // "chirality/cw"
                Color ccwColor   { 0.95f, 0.25f, 0.25f, DebugA };   // "chirality/ccw"
                Color otherColor { 0.20f, 0.85f, 0.85f, DebugA };   // anything unlabelled
                Color crossColor { 1.00f, 0.90f, 0.20f, DebugA };   // "intersections"

                // Crossing numbers are normalised so the chain's MAX level is 0:
                // 0 = blue (the outside), -1 = green (one in), -2 = red (two in),
                // deeper levels dim toward dark red.
                using Sketch::App::DisplayGroup;

                auto groupColor = [&](const Sketch::App::Stoicheion& e) -> Color {
                    switch (e.group) {
                        // The prepared profile zero: the recursion's exact seed, yellow.
                        case DisplayGroup::Profile0:     { return Color{ 1.00f, 0.85f, 0.20f, 0.90f }; }
                        // A step the toolpath sanity rule skipped: a ghost.
                        case DisplayGroup::ToolpathSkip: { return Color{ 0.55f, 0.55f, 0.60f, 0.35f }; }
                        // Production: the valid offset chains, classified by exact
                        // signed area -- positive red, negative green. Full opacity.
                        case DisplayGroup::ValidPos:     { return Color{ 0.95f, 0.30f, 0.25f, 1.00f }; }
                        case DisplayGroup::ValidNeg:     { return Color{ 0.25f, 0.90f, 0.40f, 1.00f }; }
                        // Debug: crossing-number level colouring (level in groupLevel).
                        case DisplayGroup::Crossing: {
                            int v = e.groupLevel;
                            if (v >= 0)  { return Color{ 0.30f, 0.50f, 1.00f, DebugA }; }   // max: blue
                            if (v == -1) { return Color{ 0.25f, 0.90f, 0.40f, DebugA }; }   // one in: green
                            float dim = std::max(0.40f, 1.0f - 0.22f * (-v - 2));
                            return Color{ 0.95f * dim, 0.30f * dim, 0.25f * dim, DebugA };  // two+ in: reds
                        }
                        default: { return otherColor; }
                    }
                };

                Color startColor { 1.00f, 0.45f, 0.85f, DebugA };   // "start": the walk's origin, pink (debug)

                Color minColor { 1.00f, 0.68f, 0.92f, DebugA };   // "minimum": the extracted valid path, pastel magenta (debug)

                for (const SketchGeometry& layer : app->activeProject->offsetLayers) {
                    bool arrows = app->viewArrows;
                    for (const auto& e : layer.entities) {
                        if (!e
                            || e->group == DisplayGroup::Intersections
                            || e->group == DisplayGroup::Start
                            || e->group == DisplayGroup::Minimum) { continue; }
                        appendEntity(geometry, *e, groupColor(*e), arrows);
                    }
                    // The minimum-level extraction rides on top of the level colours.
                    for (const auto& e : layer.entities) {
                        if (e && e->group == DisplayGroup::Minimum) { appendEntity(geometry, *e, minColor, arrows); }
                    }
                    for (const auto& e : layer.entities) {
                        if (!e || !e->isPoint()) { continue; }
                        if (e->group == DisplayGroup::Intersections) {
                            appendPoint(geometry, *static_cast<const Sketch::App::Point2*>(e.get()), crossColor, 5.0f);
                        }
                        else if (e->group == DisplayGroup::Start) {
                            appendPoint(geometry, *static_cast<const Sketch::App::Point2*>(e.get()), startColor, 6.5f);
                        }
                    }
                }

                // Toolpath playback overlay: the flattened chain list, pending in
                // gray, achieved in red, with the tool circle at the playhead. The
                // tool "magically" jumps between chain ends -- linking comes later.
                if (insetActive && previewBar) {

                    // The REAL toolpath: chain order and travel directions ARE the
                    // tool motion (reverse already baked in by the strategy layer).
                    const std::vector<Sketch::App::Chain>& run = app->activeProject->toolpath;

                    float total = 0.0f;
                    std::vector<float> lens;
                    lens.reserve(run.size());
                    for (const auto& c : run) {
                        float L = 0.0f;
                        for (const auto& e : c.edges) { L += Sketch::App::Chain::edgeLength(*e); }
                        lens.push_back(L);
                        total += L;
                    }

                    if (total > 1e-6f) {
                        const float target = total * (previewBar->percent * 0.01f);
                        const Color pendingColor { 0.55f, 0.55f, 0.58f, 0.85f };
                        const Color doneColor    { 0.95f, 0.20f, 0.18f, 1.00f };
                        const Color toolColor    { 1.00f, 0.80f, 0.30f, 1.00f };
                        const bool runArrows = app->viewArrows;

                        Pos playhead;
                        bool playheadSet = false;
                        float acc = 0.0f;

                        for (size_t i = 0; i < run.size(); i++) {
                            const auto& chain = run[i];

                            if (acc + lens[i] <= target) {            // fully achieved
                                for (const auto& e : chain.edges) { appendEntity(geometry, *e, doneColor, runArrows); }
                                if (!chain.edges.empty()) {
                                    playhead = Sketch::App::Chain::eEnd(*chain.edges.back());
                                    playheadSet = true;
                                }
                            }
                            else if (acc >= target) {                 // fully pending
                                for (const auto& e : chain.edges) { appendEntity(geometry, *e, pendingColor, runArrows); }
                            }
                            else {                                     // the live chain
                                float within = target - acc;
                                for (const auto& e : chain.edges) {
                                    float L = Sketch::App::Chain::edgeLength(*e);
                                    if (within >= L) {
                                        appendEntity(geometry, *e, doneColor, runArrows);
                                        within -= L;
                                    }
                                    else if (within > 0.0f) {
                                        playhead = appendEntitySplit(geometry, *e, within, doneColor, pendingColor);
                                        playheadSet = true;
                                        within = 0.0f;
                                    }
                                    else {
                                        appendEntity(geometry, *e, pendingColor, runArrows);
                                    }
                                }
                            }
                            acc += lens[i];
                        }

                        if (!playheadSet && !run.empty() && !run.front().edges.empty()) {
                            playhead = Sketch::App::Chain::eStart(*run.front().edges.front());
                            playheadSet = true;
                        }
                        if (playheadSet) {
                            appendArcSpan(geometry, playhead, app->toolRadius, 0.0f, TAU, toolColor);
                        }
                    }
                }
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

            // A view-option or parameter change (toolbar view-select group, left
            // parameter panel) needs a rebuild.
            if (app) {
                int options = (app->viewValid     ? 1 : 0)
                            | (app->viewWinding   ? 2 : 0)
                            | (app->viewDiscarded ? 4 : 0)
                            | (app->viewArrows    ? 8 : 0)
                            | (app->viewToolpath  ? 16 : 0)
                            | (app->toolReverse   ? 32 : 0)
                            | (app->climbMilling  ? 64 : 0);
                if (options != lastViewOptions) { lastViewOptions = options; geometryDirty = true; }
                if (app->toolRadius != lastToolRadius) { lastToolRadius = app->toolRadius; geometryDirty = true; }
                if (app->iterations != lastIterations) { lastIterations = app->iterations; geometryDirty = true; }
                if (app->strategy != lastStrategy) { lastStrategy = app->strategy; geometryDirty = true; }
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
