module;

#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

#include <dbg.hpp>

export module Rev.Element.Chart;

import Rev.Core.Pos;
import Rev.Core.Color;
import Rev.Core.Vertex;
import Rev.Core.Rect;
import Rev.Core.View;
import Rev.Core.Transform;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.TextBox;

import Rev.Primitive.Lines;
import Rev.Primitive.Triangles;

import Rev.Graphics.Canvas;

export namespace Rev::Element {

    namespace Styles {
        
        Style Chart = {
            .overflow = Overflow::Hide,
            .size = { .width = Grow() },
            .margin = { 4_px, 4_px, 4_px, 4_px },
            .background = { .color = rgba(255, 255, 255, 0.05) }
        };
    };

    using namespace Core;
    struct Chart : public Box {

        View view = { 0, 1, 0, 1 };
        Transform chartToScreen;
        Transform screenToChart;

        // Points in chart-space, and screen-space
        std::vector<Vertex> points;
        std::vector<Vertex> screenPoints;
        std::vector<Vertex> screenBottom;
        std::vector<Vertex> gridPoints;

        Lines* grid = nullptr;
        Triangles* fill = nullptr;
        Lines* line = nullptr;

        // Create
        Chart(Element* parent, StyleList styles = {}, std::string name = "Box") : Box(parent, styles, "Chart") {

            // Self
            this->styles = { &Styles::Chart };
            
            Graphics::Canvas* canvas = shared->canvas;

            grid = new Lines(canvas);

            fill = new Triangles(canvas, {
                .topology = Triangles::Topology::Strip,
                .left = &screenPoints, .right = &screenBottom
            });

            line = new Lines(canvas, { &screenPoints });
        }
        
        // Adjusting view
        //--------------------------------------------------

        Pos pinPos;
        View pinView;

        void mouseDown(Event& e) override {

            pinPos = screenToChart * e.mouse.pos;
            pinView = view;

            Box::mouseDown(e);
        }

        void mouseDrag(Event& e) override {

            Pos scale = screenToChart.getScale();
            Pos scaledDiff = e.mouse.diff * scale;
            scaledDiff *= { -1, 1 };
            
            view = pinView.shifted(scaledDiff);

            refresh(e);

            Box::mouseDrag(e);
        }

        void mouseWheel(Event& e) override {

            dbg("wheel");

            float wheelSensitivity = 10.0f * (1.0f / 120.0f);

            if (e.keyboard.ctrl || e.keyboard.shift) {
                                
                float scale = (e.mouse.wheel.y > 0) ? 0.9f : 1.1f;

                view.zoom(screenToChart * e.mouse.pos, {
                    e.keyboard.ctrl ? scale : 1.0f,
                    e.keyboard.shift ? scale : 1.0f
                });

                refresh(e);

                return Box::mouseWheel(e);
            }

            // Calculate movement, swap axis if shift
            Pos scale = screenToChart.getScale();
            Pos shiftBy = scale * wheelSensitivity * e.mouse.wheel;
            
            if (e.keyboard.shift) {
                shiftBy = shiftBy.swapAxis();
                shiftBy.x *= -1.0f;
            }

            // Shift view and refresh
            view.shift(shiftBy);            
            refresh(e);

            Box::mouseWheel(e);
        }

        // Computing
        //--------------------------------------------------

        float niceStep(float range) {

            // Target around 10 lines across the view
            float rough = range / 10.0f;
        
            // Find nearest power of ten
            float exponent = std::floor(std::log10(rough));
            float base = std::pow(10.0f, exponent);
        
            base *= 10.0f;
        
            return base;
        }

        // Major steps at powers of ten only
        float majorStep(float range) {
            return niceStep(range);
        }
        
        // Use the existing nice step
        float minorStep(float range) {
            return majorStep(range) / 2.0f;
        }

        // Compute grid
        void computeGrid() {

            //--------------------------------------------------
            // Setup
            //--------------------------------------------------
        
            grid->color = { 1, 1, 1, 0.25f };
            grid->strokeWidth = 1.0f;
            grid->lines.clear();
        
            float rangeX = view.r - view.l;
            float rangeY = view.b - view.t;
        
            //--------------------------------------------------
            // Determine order of magnitude and step size
            //--------------------------------------------------
        
            // Determine logarithmic scale position
            float logRangeX = std::log10(rangeX);
            float orderExpX = std::floor(logRangeX);
            float fracX = logRangeX - orderExpX; // 0..1 across current decade
        
            // Determine neighboring orders
            float orderBelowX   = std::pow(10.0f, orderExpX - 1.0f);
            float orderCurrentX = std::pow(10.0f, orderExpX);
            float orderAboveX   = std::pow(10.0f, orderExpX + 1.0f);
        
            // Step sizes for each order
            float stepBelowX   = orderBelowX;
            float stepCurrentX = orderCurrentX;
            float stepAboveX   = orderAboveX;
    
            // Determine logarithmic scale position
            float logRangeY = std::log10(rangeY);
            float orderExpY = std::floor(logRangeY);
            float fracY = logRangeY - orderExpY; // 0..1 across current decade
        
            // Determine neighboring orders
            float orderBelowY   = std::pow(10.0f, orderExpY - 1.0f);
            float orderCurrentY = std::pow(10.0f, orderExpY);
            float orderAboveY   = std::pow(10.0f, orderExpY + 1.0f);
        
            // Step sizes for each order
            float stepBelowY   = orderBelowY;
            float stepCurrentY = orderCurrentY;
            float stepAboveY   = orderAboveY;

            // Minor grid lines are two orders finer
            float stepX = stepBelowX;
            float stepY = stepBelowY;
        
            //--------------------------------------------------
            // Compute uniform alpha multiplier (zoom fade)
            //--------------------------------------------------
        
            auto zoomFade = [](float range) {
                float logRange = std::log10(range);
                float frac = logRange - std::floor(logRange);
                return std::clamp(1.0f - frac, 0.0f, 1.0f);
            };
        
            float uniformAlphaX = zoomFade(rangeX);
            float uniformAlphaY = zoomFade(rangeY);
        
            //--------------------------------------------------
            // Alpha per line: smooth transition across orders
            //--------------------------------------------------

            auto alphaForValue = [&](float value, bool horizontal) -> float {

                float frac = fracY;
            
                float stepAbove = stepAboveY;
                float stepBelow = stepBelowY;
                float stepCurrent = stepCurrentY;

                if (horizontal) {
                    frac = fracX;
                    stepAbove = stepAboveX;
                    stepBelow = stepBelowX;
                    stepCurrent = stepCurrentX;
                }

                // Helper: detect alignment with a step
                auto isMultipleOf = [&](float value, float step, float tighterTol = 0.0001f) {
                    float mod = std::fmod(std::fabs(value), step);
                    float proximity = std::min(mod, step - mod);
                    float tol = std::max(step * tighterTol, 1e-6f); // dynamic small tolerance
                    return (proximity < tol);
                };
            
                // 1. Lines aligned to the *next higher* order (major decades)
                if (isMultipleOf(value, stepAbove))
                    return 1.0f; // Always full bright

                // 2. Lines aligned to the *current* order
                if (isMultipleOf(value, stepCurrent, 0.001f)) {
                    // Fade from bright (zoomed in) to dim (zoomed out)
                    float t = std::clamp(1.0f - frac, 0.0f, 1.0f);
                    float fade = t * t; // quadratic
                    return 0.2f + 0.8f * fade; // 1.0 → 0.2
                }

                // 3. Everything else = minor lines
                // Fade out completely before reaching the next order boundary
                float t = std::clamp(frac / 0.85f, 0.0f, 1.0f);
                float fade = 1 - frac;
                return 0.2f * fade;
            };
            
        
            //--------------------------------------------------
            // Compute grid extents
            //--------------------------------------------------
        
            float xStart = std::floor(view.l / stepX) * stepX;
            float xEnd   = std::ceil(view.r / stepX) * stepX;
            float yStart = std::floor(view.t / stepY) * stepY;
            float yEnd   = std::ceil(view.b / stepY) * stepY;
        
            //--------------------------------------------------
            // Create vertical lines
            //--------------------------------------------------
        
            for (float x = xStart; x <= xEnd + stepX * 0.5f; x += stepX) {
                float baseAlpha = alphaForValue(x, true);
                Core::Color color = { 1, 1, 1, baseAlpha };
                grid->lines.push_back({
                    .points = { { x, view.t, color }, { x, view.b, color } }
                });
            }
        
            //--------------------------------------------------
            // Create horizontal lines
            //--------------------------------------------------
        
            for (float y = yStart; y <= yEnd + stepY * 0.5f; y += stepY) {
                float baseAlpha = alphaForValue(y, false);
                Core::Color color = { 1, 1, 1, baseAlpha};
                grid->lines.push_back({
                    .points = { { view.l, y, color }, { view.r, y, color } }
                });
            }
        
            //--------------------------------------------------
            // Transform to screen space
            //--------------------------------------------------
        
            for (Lines::Line& line : grid->lines) {
                for (Vertex& point : line.getPoints()) {
                    point = chartToScreen * point;
                }
            }
        
            grid->compute();
        }

        void computeLines() {

            // Set color
            line->color = { 1, 0, 0, 1 };
            fill->color = { 1, 0, 0, 0.5 };

            screenPoints.resize(points.size());
            screenBottom.resize(points.size());
            
            for (size_t i = 0; i < points.size(); i++) {
                screenBottom[i] = { points[i].x, 0.5, { 1, 0, 0, 0.1 } };
            }

            screenPoints = points;

            for (Vertex& point : screenPoints) { point = chartToScreen * point; }
            for (Vertex& point : screenBottom) { point = chartToScreen * point; }

            fill->compute();
            line->compute();
        }

        void computePrimitives(Event& e) override {

            // Calculate transforms
            //--------------------------------------------------

            Pos viewSpan = view.span();

            chartToScreen = Transform::Translation(rect.x, rect.y + rect.h) *
                            Transform::Scale(rect.w / viewSpan.x, -rect.h / viewSpan.y) *
                            Transform::Translation(-view.l, -view.t);

            screenToChart = Transform::Inverse(chartToScreen);
            
            this->computeGrid();
            this->computeLines();

            Box::computePrimitives(e);
        }

        void draw(Event& e) override {

            Box::draw(e);

            grid->draw();
            fill->draw();
            line->draw();
        }
    };
};