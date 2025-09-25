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
            /*Box* greyBox = new Box(this, "GreyBox");
            greyBox->style = {
                .size = { .width = Shrink(), .maxWidth = 600_px, .maxHeight = 400_px },
                .padding = { 10_px, 10_px, 10_px, 10_px },
                .border = { .radius = 10_px },
                .background { .color = rgba(255, 255, 255, 0.1) },
            };*/
        }

        // Destroy
        ~MacInterface() {

        }
    };
};