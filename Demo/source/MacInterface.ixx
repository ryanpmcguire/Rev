module;

#include <cstddef>

export module MacInterface;

import Rev.Style;
import Rev.Element;
import Rev.Box;
import Rev.TextBox;

import Rev.Slider;

import Resources.Fonts.Arial.Arial_ttf;

export namespace HelloWorld {

    using namespace Rev;

    struct MacInterface : public Box {

        // Create
        MacInterface(Element* parent) : Box(parent) {

            // Self
            this->style->alignment = { Axis::Vertical, Align::Center, Align::Center };
            this->style->background.color = rgba(25, 25, 25, 1.0);
            this->style->size = { .width = 100_pct, .height = 100_pct };
            this->style->padding = { 40_px, 40_px, 40_px, 40_px };

            // Grey box
            //for (size_t i = 0; i < 1000; i++) {

                Box* greyBox = new Box(this, "GreyBox");
                greyBox->style = {
                    .size = { .width = Grow(), .height = Grow(), .maxWidth = 600_px, .maxHeight = 400_px },
                    .alignment = { .horizontal = Align::Center, .vertical = Align::Center },
                    .padding = { 10_px, 10_px, 10_px, 10_px },
                    .margin = { 5_px, 5_px, 5_px, 5_px },
                    .border = { .radius = 10_px },
                    .background { .color = rgba(255, 255, 255, 0.1) },
                };
            //}

            TextBox* text = new TextBox(greyBox, "Hello World");
            text->style->text.size = 18_px;
        }

        // Destroy
        ~MacInterface() {

        }
    };
};