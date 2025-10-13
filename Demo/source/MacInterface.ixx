module;

#include <cstddef>

export module MacInterface;

import Rev.Style;
import Rev.Element;
import Rev.Box;
import Rev.TextBox;

import Rev.Slider;
import Rev.Chart;

import Resources.Fonts.Arial.Arial_ttf;

export namespace HelloWorld {

    using namespace Rev;

    struct MacInterface : public Box {

        // Create
        MacInterface(Element* parent) : Box(parent) {

            // Self
            this->style->alignment = { Axis::Horizontal, Align::Center, Align::Center };
            this->style->background.color = rgba(25, 25, 25, 1.0);
            this->style->size = { .width = 100_pct, .height = 100_pct };
            this->style->padding = { 40_px, 40_px, 40_px, 40_px };

            // Grey box
            //for (size_t i = 0; i < 1000; i++) {

                Box* greyBox = new Box(this, "GreyBox");
                greyBox->style = {
                    .size = { .width = Grow(), .height = Grow(), .maxWidth = 600_px, .maxHeight = 400_px },
                    .alignment = { Axis::Horizontal, Align::Center, Align::Center },
                    //.padding = { 10_px, 10_px, 10_px, 10_px },
                    .margin = { 5_px, 5_px, 5_px, 5_px },
                    .border = { .radius = 10_px },
                    .background { .color = rgba(255, 255, 255, 0.1) },
                };
            //}

            TextBox* text = new TextBox(greyBox, "Hello World");
            text->style->text.size = 32_px;
            text->style->background.color = rgba(1, 0, 0, 0.2);

            Chart* chart = new Chart(greyBox);
            
            chart->style = {
                .size = { .width = Grow(), .height = Grow(),  .maxWidth = 100_pct, .minHeight = 100_px },
                .background { .color = rgba(0, 0, 0, 0.1) },
            };

            chart->data = { { 0, 0 }, { 0.5, 1.0 }, { 1.0, 0.5 } };

            Slider* slider = new Slider(chart);
        }

        // Destroy
        ~MacInterface() {

        }
    };
};