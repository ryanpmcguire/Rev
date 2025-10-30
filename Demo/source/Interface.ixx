module;

#include <cstddef>
#include <cmath>

export module Interface;

import Rev.Element;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.TextBox;
import Rev.Element.Slider;
import Rev.Element.Chart;

import Resources.Fonts.Arial.Arial_ttf;

export namespace HelloWorld {

    using namespace Rev;
    using namespace Rev::Element;

    struct Interface : public Box {

        // Create
        Interface(Element* parent) : Box(parent) {

            // Self
            this->style->alignment = { Axis::Horizontal, Align::Center, Align::Center };
            this->style->background.color = rgba(25, 25, 25, 1.0);
            this->style->size = { .width = 100_pct, .height = 100_pct };
            this->style->padding = { 40_px, 40_px, 40_px, 40_px };

            Box* greyBox = new Box(this, "GreyBox");
            greyBox->style = {
                .size = { .width = Grow(), .height = Grow(), .maxWidth = 2000_px, .maxHeight = 2000_px },
                .alignment = { Axis::Horizontal, Align::Center, Align::Center },
                .padding = { 10_px, 10_px, 10_px, 10_px },
                .margin = { 5_px, 5_px, 5_px, 5_px },
                .border = { .radius = 10_px },
                .background { .color = rgba(255, 255, 255, 0.05) },
            };

                TextBox* text = new TextBox(greyBox, "Hello World");
                text->style->text.size = 32_px;
                text->style->background.color = rgba(1, 0, 0, 0.2);

                Chart* chart = new Chart(greyBox);
                
                chart->style = {
                    .size = { .width = Grow(), .height = Grow(),  .maxWidth = 100_pct, .minHeight = 100_px },
                };

                size_t num = 1000;
                for (size_t i = 0; i < num; i++) {
                    float t = float(i) / float(num);
                    chart->points.push_back({ t, 0.5f + 0.5f * sin(10.0f * 3.14159f * t) });
                }

                Slider* slider = new Slider(greyBox);
        }

        // Destroy
        ~Interface() {

        }
    };
};