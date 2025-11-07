module;

#include <cstddef>
#include <cmath>

export module Interface;

import Rev.Element;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Slider;
import Rev.Element.Dropdown;
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
            this->style->background.color = rgba(1, 0, 0, 0.1);
            this->style->size = { .width = 100_pct, .height = 100_pct };
            //this->style->padding = { 40_px, 40_px, 40_px, 40_px };

            Box* greyBox = new Box(this, {}, "GreyBox");
            greyBox->style = {
                .alignment = { Axis::Horizontal, Align::Center, Align::Center },
                .size = { .width = Grow(), .height = Grow() },
                .margin = { 5_px, 5_px, 5_px, 5_px },
                .padding = { 10_px, 10_px, 10_px, 10_px },
                .background { .color = rgba(0, 0, 0, 0.05) },
                .border = { .radius = 10_px, .bottom = { .color = rgba(255, 0, 0, 1), .width = 1_px } },
                .shadow = { .color = rgba(0, 0, 0, 0.5), .size = Px(-10), .blur = 20_px }
            };

            Text* text = new Text(greyBox, "Hello");
                text->style->text.size = 32_px;
                text->style->text.color = rgba(0, 0, 0, 1);
                text->style->background.color = rgba(1, 0, 0, 0.2);

                Chart* chart = new Chart(greyBox);
                
                chart->style = {
                    .size = { .width = 100_pct, .height = Grow(), .minHeight = 100_px },
                    .border = { .color = rgba(0, 0, 0, 0.1), .radius = 10_px, .width = 1_px, .transition = 200 }
                };

                size_t num = 100;
                for (size_t i = 0; i < num; i++) {
                    float t = float(i) / float(num);
                    chart->points.push_back({ t, 0.5f + 0.5f * sin(10.0f * 3.14159f * t) });
                }

                Slider* slider = new Slider(greyBox);

                Dropdown* dropdown = new Dropdown(greyBox);
        }

        // Destroy
        ~Interface() {

        }
    };
};