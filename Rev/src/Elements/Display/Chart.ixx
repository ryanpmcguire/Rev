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

export namespace Rev {

    namespace Styles {
        
        Style Chart = {
            .size = { .width = Grow() },
            .margin = { 4_px, 4_px, 4_px, 4_px },
        };
    };

    struct Chart : public Box {

        struct Point {
            float x, y;
        };

        std::vector<Point> data;

        // Create
        Chart(Element* parent) : Box(parent, "Chart") {

            // Self
            this->styles = { &Styles::Chart };
        }

        void computeStyle(Event& e) override {
        
            Box::computeStyle(e);
        }

        void computePrimitives(Event& e) override {

            Box::computePrimitives(e);
        }
    };
};