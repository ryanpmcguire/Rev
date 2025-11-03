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
            this->style->background.color = rgba(0, 0, 0, 0.0);
            this->style->size = { .width = 100_pct, .height = 100_pct };
            this->style->padding = { 40_px, 40_px, 40_px, 40_px };

            Box* greyBox = new Box(this, {}, "GreyBox");
            greyBox->style = {
                .size = { .width = Grow(), .height = Grow(), .maxWidth = 2000_px, .maxHeight = 2000_px },
                .alignment = { Axis::Horizontal, Align::Center, Align::Center },
                .padding = { 10_px, 10_px, 10_px, 10_px },
                .margin = { 5_px, 5_px, 5_px, 5_px },
                .background { .color = rgba(0, 0, 0, 0.05) },
                .border = { .radius = 10_px },
                .shadow = { .blur = 20_px, .size = Px(-10), .color = rgba(0, 0, 0, 0.5) }
            };

                TextBox* text = new TextBox(greyBox, "Hello World");
                text->style->text.size = 32_px;
                text->style->text.color = rgba(0, 0, 0, 1);
                text->style->background.color = rgba(1, 0, 0, 0.2);

                Chart* chart = new Chart(greyBox);
                
                chart->style = {
                    .size = { .width = Grow(), .height = Grow(),  .maxWidth = 100_pct, .minHeight = 100_px },
                    .border = { .radius = 10_px, .width = 1_px, .color = rgba(0, 0, 0, 0.1), .transition = 200 }
                };

                chart->hoverStyle = {
                    .border = { .width = 4_px }
                };

                size_t num = 1000;
                for (size_t i = 0; i < num; i++) {
                    float t = float(i) / float(num);
                    chart->points.push_back({ t, 0.5f + 0.5f * sin(10.0f * 3.14159f * t) });
                }

                Slider* slider = new Slider(greyBox);

                slider->sliderContainer->style = {
                    .shadow = { .transition = 200 }
                };

                slider->sliderContainer->hoverStyle = {
                    .shadow = { .size = Px(-5), .blur = 10_px, .color = rgba(0, 0, 0, 0.5), .x = 2_px, .y = 2_px }
                };
        }

        // Destroy
        ~Interface() {

        }
    };
};