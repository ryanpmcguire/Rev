module;

#include <cmath>
#include <vector>
#include <algorithm>

export module Rev.Element.Chart;

import Rev.Core.Pos;
import Rev.Core.Vertex;
import Rev.Core.Rect;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.TextBox;

import Rev.Primitive.Lines;
import Rev.Primitive.Triangles;

export namespace Rev::Element {

    namespace Styles {
        
        Style Chart = {
            .size = { .width = Grow() },
            .margin = { 4_px, 4_px, 4_px, 4_px },
        };
    };

    struct Chart : public Box {

        // Points in chart-space, and screen-space
        std::vector<Vertex> points;
        std::vector<Vertex> screenPoints;
        std::vector<Vertex> screenBottom;

        Triangles* fill = nullptr;
        Lines* line = nullptr;

        // Create
        Chart(Element* parent) : Box(parent, "Chart") {

            // Self
            this->styles = { &Styles::Chart };

            line = new Lines(topLevelDetails->canvas, &screenPoints);

            fill = new Triangles(topLevelDetails->canvas, {
                .topology = Triangles::Topology::Strip,
                .left = &screenPoints, .right = &screenBottom
            });
        }

        void computeStyle(Event& e) override {
        
            Box::computeStyle(e);
        }

        void computePrimitives(Event& e) override {

            screenPoints.resize(points.size());
            screenBottom.resize(points.size());

            line->data->color = { 1, 0, 0, 1.0 };
            line->data->strokeWidth = 2.0f;

            fill->data->color = { 1, 0, 0, 0.0 };

            Rect flippedRect = {
                rect.x, rect.y + rect.h,
                rect.w, -1.0f * rect.h
            };

            for (size_t i = 0; i < points.size(); i++) {

                Vertex& chartPoint = points[i];
                Vertex& screenPoint = screenPoints[i];
                Vertex& bottomPoint = screenBottom[i];

                bottomPoint = { chartPoint.x, 0.5 };

                screenPoint = flippedRect.relToAbs(chartPoint);
                bottomPoint = flippedRect.relToAbs(bottomPoint);

                bottomPoint.r = 1; bottomPoint.g = 0; bottomPoint.b = 0; bottomPoint.a = 0.1;
                screenPoint.r = 1; screenPoint.g = 0; screenPoint.b = 0; screenPoint.a = 0.4;
            }

            fill->compute();
            line->compute();

            Box::computePrimitives(e);
        }

        void draw(Event& e) override {

            Box::draw(e);

            fill->draw();
            line->draw();
        }
    };
};