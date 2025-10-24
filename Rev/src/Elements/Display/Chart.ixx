module;

#include <cmath>
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
            .size = { .width = Grow() },
            .margin = { 4_px, 4_px, 4_px, 4_px },
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
        Chart(Element* parent) : Box(parent, "Chart") {

            // Self
            this->styles = { &Styles::Chart };
            
            Graphics::Canvas* canvas = topLevelDetails->canvas;

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
            Pos shiftBy = {
                (wheelSensitivity * e.mouse.wheel.x),
                (wheelSensitivity * e.mouse.wheel.y)
            };

            Pos scale = screenToChart.getScale();
            shiftBy *= scale;
            
            if (e.keyboard.shift) {
                shiftBy = shiftBy.swapAxis();
                shiftBy.x *= -1.0f;
            }

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

        void computePrimitives(Event& e) override {

            // Calculate transforms
            //--------------------------------------------------

            Pos viewSpan = view.span();

            chartToScreen = Transform::Translation(rect.x, rect.y + rect.h) *
                            Transform::Scale(rect.w / viewSpan.x, -rect.h / viewSpan.y) *
                            Transform::Translation(-view.l, -view.t);

            screenToChart = Transform::Inverse(chartToScreen);
            
            // Calculate grid
            //--------------------------------------------------

            grid->color = { 0.0, 0, 0, 0.0 };
            grid->strokeWidth = 1.0f;
            grid->lines.clear();

            float rangeX = view.r - view.l;
            float logScale = std::log10(rangeX);
            float frac = logScale - std::floor(logScale);
            float zoomFade = 1.0f - frac;

            float xMajor = majorStep(view.r - view.l);
            float yMajor = majorStep(view.b - view.t);

            float xMinor = xMajor / 10.0f;
            float yMinor = yMajor / 10.0f;

            float xMid = xMajor / 2.0f;
            float yMid = yMajor / 2.0f;

            // Compute fade as ratio of step-to-view
            auto fade = [](float step, float viewRange) {
                return std::clamp(step / viewRange, 0.0f, 1.0f);
            };

            float majorFadeX = fade(xMajor, view.r - view.l);
            float midFadeX   = fade(xMid,   view.r - view.l);
            float minorFadeX = fade(xMinor, view.r - view.l);

            float majorFadeY = fade(yMajor, view.b - view.t);
            float midFadeY   = fade(yMid,   view.b - view.t);
            float minorFadeY = fade(yMinor, view.b - view.t);

            // Combine both axes (average)
            float majorFade = 0.5f * (majorFadeX + majorFadeY);
            float midFade   = 0.5f * (midFadeX   + midFadeY);
            float minorFade = 0.5f * (minorFadeX + minorFadeY);

            // Colors
            Core::Color majorColor = { 1, 1, 1, majorFade };
            Core::Color midColor   = { 1, 1, 1, midFade   };
            Core::Color minorColor = { 1, 1, 1, minorFade };

            // Major lines
            float xStartMajor = std::floor(view.l / xMajor) * xMajor;
            float xEndMajor   = std::ceil(view.r / xMajor) * xMajor;
            float yStartMajor = std::floor(view.t / yMajor) * yMajor;
            float yEndMajor   = std::ceil(view.b / yMajor) * yMajor;

            for (float x = xStartMajor; x <= xEndMajor + xMajor * 0.5f; x += xMajor)
                grid->lines.push_back({ .points = { {x, view.t, majorColor}, {x, view.b, majorColor} } });

            for (float y = yStartMajor; y <= yEndMajor + yMajor * 0.5f; y += yMajor)
                grid->lines.push_back({ .points = { {view.l, y, majorColor}, {view.r, y, majorColor} } });

            // Minor lines
            float xStartMinor = std::floor(view.l / xMinor) * xMinor;
            float xEndMinor   = std::ceil(view.r / xMinor) * xMinor;
            float yStartMinor = std::floor(view.t / yMinor) * yMinor;
            float yEndMinor   = std::ceil(view.b / yMinor) * yMinor;

            for (float x = xStartMinor; x <= xEndMinor + xMinor * 0.5f; x += xMinor) {
                grid->lines.push_back({ .points = { {x, view.t, minorColor}, {x, view.b, minorColor} } });
            }

            for (float y = yStartMinor; y <= yEndMinor + yMinor * 0.5f; y += yMinor) {
                grid->lines.push_back({ .points = { {view.l, y, minorColor}, {view.r, y, minorColor} } });
            }

            // Mid lines
            float xStartMid = std::floor(view.l / xMid) * xMid;
            float xEndMid   = std::ceil(view.r / xMid) * xMid;
            float yStartMid = std::floor(view.t / yMid) * yMid;
            float yEndMid   = std::ceil(view.b / yMid) * yMid;

            for (float x = xStartMid; x <= xEndMid + xMid * 0.5f; x += xMid) {
                grid->lines.push_back({ .points = { {x, view.t, midColor}, {x, view.b, midColor} } });
            }

            for (float y = yStartMid; y <= yEndMid + yMid * 0.5f; y += yMid) {
                grid->lines.push_back({ .points = { {view.l, y, midColor}, {view.r, y, midColor} } });
            }

            // Transform to screen space
            for (Lines::Line& line : grid->lines) {
                for (Vertex& point : line.getPoints()) {
                    point = chartToScreen * point;
                }
            }

            // Calculate line(s)
            //--------------------------------------------------
            
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

            grid->compute();
            fill->compute();
            line->compute();

            Box::computePrimitives(e);
        }

        void draw(Event& e) override {

            topLevelDetails->canvas->stencilWrite(true);
            topLevelDetails->canvas->fillStencil(0);
            Box::draw(e);
            topLevelDetails->canvas->stencilWrite(false);

            grid->draw();
            fill->draw();
            line->draw();

            topLevelDetails->canvas->stencilWrite(true);
            topLevelDetails->canvas->fillStencil(1);
            topLevelDetails->canvas->stencilWrite(false);
        }
    };
};