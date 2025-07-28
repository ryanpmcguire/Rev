module;

export module Interface;

import Rev.Element;
import Rev.Box;

import Rev.Slider;

export namespace HelloWorld {

    using namespace Rev;

    struct Interface : public Box {

        // Create
        Interface(Element* parent) : Box(parent) {

            // Self
            this->style.alignment = { Axis::Vertical, Align::Center, Align::Center };
            this->style.background.color = Color(0.1, 0.1, 0.1, 1);
            this->style.size = { .width = 100_pct, .height = 100_pct };

            // Red box
            Box* greyBox = new Box(this, "RedBox");
            greyBox->style = {
                .size = { .width = Grow(), .height = Grow(), .maxWidth = 300_px, .maxHeight = 400_px },
                .padding = { 10_px, 10_px, 10_px, 10_px },
                .border = { .radius = 10_px },
                .background { .color = Color(1, 1, 1, 0.1) },
            };

            /*for (size_t i = 0; i < 2; i++) {
                Slider* slider = new Slider(greyBox);
            }*/

            Slider* sliderA = new Slider(greyBox, { .val = 0.0 });
            Slider* sliderB = new Slider(greyBox, { .val = 1.0 });
        }

        // Destroy
        ~Interface() {

        }
    };
};