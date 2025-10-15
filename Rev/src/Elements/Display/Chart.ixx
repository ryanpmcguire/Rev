module;

#include <cmath>
#include <vector>
#include <algorithm>

export module Rev.Chart;

import Rev.Pos;
import Rev.Event;
import Rev.Style;
import Rev.Element;
import Rev.Box;
import Rev.TextBox;

import Rev.Graphics.Lines;

export namespace Rev {

    namespace Styles {
        
        Style Chart = {
            .size = { .width = Grow() },
            .margin = { 4_px, 4_px, 4_px, 4_px },
        };
    };

    struct Chart : public Box {

        std::vector<Pos> points = {
            { 0, 0 }, { 0.5, 1.0 }, { 1.0, 0.5 }
        };

        Lines* lines = nullptr;

        // Create
        Chart(Element* parent) : Box(parent, "Chart") {

            // Self
            this->styles = { &Styles::Chart };

            lines = new Lines(topLevelDetails->canvas);

            lines->points = points;
        }

        void computeStyle(Event& e) override {
        
            Box::computeStyle(e);
        }

        void computePrimitives(Event& e) override {

            lines->points.resize(points.size());
            lines->data->color = { 0, 1, 0, 0.5 };
            lines->data->strokeWidth = 10.0;

            for (size_t i = 0; i < points.size(); i++) {

                Pos& chartPoint = points[i];
                Pos& linePoint = lines->points[i];

                linePoint = this->rect.relToAbs(chartPoint);
            }

            lines->compute();

            Box::computePrimitives(e);
        }

        void draw(Event& e) override {

            lines->draw();

            Box::draw(e);
        }
    };
};