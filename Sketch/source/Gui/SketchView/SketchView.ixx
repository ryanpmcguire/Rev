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
import Sketch.Gui.Theme;

export namespace Sketch::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    using Rev::Core::Vertex;
    using Rev::Core::Color;

    // A 2D sketch canvas: the flat cousin of the world view.
    //
    // Camera model is intentionally CPU-only for now — no GPU transform. We keep
    // a world origin (in pixels from the element's top-left) and a zoom (pixels
    // per world unit), and map world -> screen by hand each frame. Segments are
    // drawn with the Lines primitive in screen space (same approach as Chart).
    //
    //   - Right-drag  : pan
    //   - Mouse wheel : zoom, keeping the point under the cursor invariant
    //   - Left-drag   : rubber-band a new segment; release commits it
    struct SketchView : public Box {

        Sketch::App::AppState* app = nullptr;

        Lines* axes = nullptr;       // gnomon: world X / Y axes
        Lines* geometry = nullptr;   // committed segments + live preview

        // CPU camera
        float originX = 0.0f;        // screen px of world x=0, from rect left
        float originY = 0.0f;        // screen px of world y=0, from rect top
        float scale   = 40.0f;       // pixels per world unit
        bool  initialized = false;

        // Origin captured at the start of a pan. e.mouse.diff is cumulative from
        // the press point, so we anchor to this rather than accumulating.
        float panOriginX = 0.0f;
        float panOriginY = 0.0f;

        // In-progress segment (left-drag)
        bool   drawing = false;
        double startX = 0.0, startY = 0.0;
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

            // Left button begins a new segment.
            if (e.mouse.lb) {
                screenToWorld(e.mouse.pos.x, e.mouse.pos.y, startX, startY);
                cursorX = startX;
                cursorY = startY;
                drawing = true;
                refresh(e);
                e.propagate = false;
            }

            // Right button begins a pan: pin the current origin.
            if (e.mouse.rb) {
                panOriginX = originX;
                panOriginY = originY;
            }
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

            // Rubber-band the in-progress segment with the left button.
            if (e.mouse.lb && drawing) {
                screenToWorld(e.mouse.pos.x, e.mouse.pos.y, cursorX, cursorY);
                refresh(e);
                e.propagate = false;
                return;
            }

            Box::mouseDrag(e);
        }

        void mouseUp(Event& e) override {

            // Commit the segment on left release.
            if (drawing && !e.mouse.lb) {

                double endX, endY;
                screenToWorld(e.mouse.pos.x, e.mouse.pos.y, endX, endY);

                drawing = false;

                // Ignore zero-length / accidental clicks (< ~3px on screen).
                double dx = endX - startX;
                double dy = endY - startY;

                if (std::hypot(dx, dy) * scale > 3.0 && app && app->activeProject) {
                    app->activeProject->addSegment({ startX, startY, endX, endY });
                }

                refresh(e);
                e.propagate = false;
                return;
            }

            Box::mouseUp(e);
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

        void buildGeometry() {

            geometry->lines.clear();

            Color segColor{ 0.86f, 0.87f, 0.90f, 1.0f };
            Color previewColor{ 0.55f, 0.62f, 0.95f, 1.0f };

            if (app && app->activeProject) {
                for (const Sketch::App::Segment2& s : app->activeProject->segments) {
                    geometry->lines.push_back({
                        .points = { worldToScreen(s.ax, s.ay), worldToScreen(s.bx, s.by) },
                        .color = segColor
                    });
                }
            }

            if (drawing) {
                geometry->lines.push_back({
                    .points = { worldToScreen(startX, startY), worldToScreen(cursorX, cursorY) },
                    .color = previewColor
                });
            }

            geometry->compute();
        }

        void computePrimitives(Event& e) override {

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
