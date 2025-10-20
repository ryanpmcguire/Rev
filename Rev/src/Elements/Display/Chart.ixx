module;

#include <cmath>
#include <vector>
#include <algorithm>

export module Rev.Element.Chart;

import Rev.Core.Pos;
import Rev.Core.Color;
import Rev.Core.Vertex;
import Rev.Core.Rect;
import Rev.Core.View;

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

    struct Chart : public Box {

        Core::View view = { 0, 0, 0.5, 0.5 };

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

        View pinView;

        void mouseDown(Event& e) override {

            pinView = view;

            Box::mouseDown(e);
        }

        void mouseDrag(Event& e) override {

            view.x = pinView.x - e.mouse.diff.x / rect.w;
            view.y = pinView.y + e.mouse.diff.y / rect.h;

            refresh(e);

            Box::mouseDrag(e);
        }

        void computeStyle(Event& e) override {
        
            Box::computeStyle(e);
        }

        void computePrimitives(Event& e) override {

            screenPoints.resize(points.size());
            screenBottom.resize(points.size());

            line->color = { 1, 0, 0, 1 };
            fill->color = { 1, 0, 0, 0.5 };

            View flippedRect = {
                rect.x, rect.y + rect.h,
                rect.w, -1.0f * rect.h
            };

            View screenView = flippedRect.transform(view);
            
            // Calculate grid points
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
                screenView.transform(line.points, line.points);
            }

            // Calculate line points
            //--------------------------------------------------

            screenPoints.resize(points.size());
            screenBottom.resize(points.size());
            
            for (size_t i = 0; i < points.size(); i++) {
                screenBottom[i] = { points[i].x, 0.5, { 1, 0, 0, 0.1 } };
            }

            screenView.transform(points, screenPoints);
            screenView.transform(screenBottom);

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