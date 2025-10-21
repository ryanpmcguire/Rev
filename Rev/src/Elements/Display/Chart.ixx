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

        View chartView = { 0, 1, 0, 1 };
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
            pinView = chartView;

            Box::mouseDown(e);
        }

        void mouseDrag(Event& e) override {

            Pos scale = screenToChart.getScale();
            Pos scaledDiff = e.mouse.diff * scale;
            scaledDiff *= { -1, 1 };
            
            chartView = pinView.shifted(scaledDiff);

            refresh(e);

            Box::mouseDrag(e);
        }

        void mouseWheel(Event& e) override {

            dbg("wheel");

            float wheelSensitivity = 10.0f * (1.0f / 120.0f);

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

            chartView.shift(shiftBy);
            
            refresh(e);

            Box::mouseWheel(e);
        }

        // Computing
        //--------------------------------------------------

        void computePrimitives(Event& e) override {

            // Calculate transforms
            //--------------------------------------------------

            Pos viewSpan = chartView.span();

            chartToScreen = Transform::Translation(rect.x, rect.y + rect.h) *
                            Transform::Scale(rect.w / viewSpan.x, -rect.h / viewSpan.y) *
                            Transform::Translation(-chartView.l, -chartView.t);

            screenToChart = Transform::Inverse(chartToScreen);
            
            // Calculate grid
            //--------------------------------------------------

            grid->color = { 1, 1, 1, 1.0 };
            grid->strokeWidth = 0.25f;

            Core::Color color = { 1, 1, 1, 1 };

            // Four vertical, four horizontal
            grid->lines = {
                { .points = { { 0.0, 0.25, color }, { 1.0, 0.25, color } } },
                { .points = { { 0.0, 0.5, color }, { 1.0, 0.5, color } } },
                { .points = { { 0.0, 0.75, color }, { 1.0, 0.75, color } } },
                { .points = { { 0.25, 0.0, color }, { 0.25, 1.0, color } } },
                { .points = { { 0.5, 0.0, color }, { 0.5, 1.0, color } } },
                { .points = { { 0.75, 0.0, color }, { 0.75, 1.0, color } } }
            };

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

            Box::draw(e);

            grid->draw();
            fill->draw();
            line->draw();
        }
    };
};