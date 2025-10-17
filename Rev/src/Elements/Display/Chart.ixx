module;

#include <cmath>
#include <vector>
#include <algorithm>

export module Rev.Chart;

import Rev.Pos;
import Rev.Rect;
import Rev.Event;
import Rev.Style;
import Rev.Element;
import Rev.Box;
import Rev.TextBox;

import Rev.Graphics.Lines;
import Rev.Graphics.Triangles;

export namespace Rev {

    namespace Styles {
        
        Style Chart = {
            .size = { .width = Grow() },
            .margin = { 4_px, 4_px, 4_px, 4_px },
        };
    };

    struct Chart : public Box {

        // Points in chart-space, and screen-space
        std::vector<Pos> points;
        std::vector<Pos> screenPoints;
        std::vector<Pos> screenBottom;

        Triangles* shading = nullptr;
        Lines* lines = nullptr;

        // Create
        Chart(Element* parent) : Box(parent, "Chart") {

            // Self
            this->styles = { &Styles::Chart };

            lines = new Lines(topLevelDetails->canvas, &screenPoints);

            shading = new Triangles(topLevelDetails->canvas, {
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

            lines->data->color = { 1, 0, 0, 0.5 };
            lines->data->strokeWidth = 4.0f;

            Rect flippedRect = {
                rect.x, rect.y + rect.h,
                rect.w, -1.0f * rect.h
            };

            for (size_t i = 0; i < points.size(); i++) {

                Pos& chartPoint = points[i];
                Pos& screenPoint = screenPoints[i];
                Pos& bottomPoint = screenBottom[i];

                bottomPoint = { chartPoint.x, 0 };

                screenPoint = flippedRect.relToAbs(chartPoint);
                bottomPoint = flippedRect.relToAbs(bottomPoint);
            }

            shading->compute();
            lines->compute();

            Box::computePrimitives(e);
        }

        void draw(Event& e) override {

            Box::draw(e);

            shading->draw();
            lines->draw();
        }
    };
};