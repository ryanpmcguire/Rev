module;

export module Interface;

import Rev.Element;
import Rev.Box;
import Rev.TextBox;

import Rev.Slider;

export namespace HelloWorld {

    using namespace Rev;

    struct Interface : public Box {
        
        Style textBoxChildStyle = {
            .size = { 10_px, 10_px },
            .margin = { 4_px, 4_px, 4_px, 4_px },
            .background = { .color = Color(1, 0.5, 0.5, 1.0) }
        };

        // Create
        Interface(Element* parent) : Box(parent) {

            // Self
            this->style->alignment = { Axis::Vertical, Align::Center, Align::Center };
            this->style->background.color = Color(0.1, 0.1, 0.1, 1);
            this->style->size = { .width = 100_pct, .height = 100_pct };
            this->style->padding = { 40_px, 40_px, 40_px, 40_px };

            // Grey box
            Box* greyBox = new Box(this, "RedBox");
            greyBox->style = {
                .size = { .width = Shrink(), .maxWidth = 600_px, .maxHeight = 400_px },
                .padding = { 10_px, 10_px, 10_px, 10_px },
                .border = { .radius = 10_px },
                .background { .color = Color(1, 1, 1, 0.1) },
            };

            Slider* sliderA = new Slider(greyBox, { .val = 0.0 });
            //Slider* sliderB = new Slider(greyBox, { .val = 1.0 });

            /*sliderA->style.size.minWidth = 100_px;
            sliderB->style.size.minWidth = 50_px;*/

            //TextBox* textBox = new TextBox(greyBox);

            /*Box* testBox = new Box(greyBox, "TestBox");
            testBox->style = {
                .size = { },
                .margin = { 4_px, 4_px, 4_px, 4_px },
                .padding = { 10_px, 10_px, 10_px, 10_px },
                .border = { .radius = 10_px },
                .background { .color = Color(1, 1, 1, 0.1) },
            };

                for (size_t i = 0; i < 25; i++) {
                    Box* testBoxChild = new Box(testBox);
                    testBoxChild->style = {
                        .size = { 10_px, 10_px },
                        .margin = { 4_px, 4_px, 4_px, 4_px },
                        .background = { .color = Color(1, 0.5, 0.5, 1.0) }
                    };
                }*/

            /*Box* testBox2 = new Box(testBox, "TestBox");
            testBox2->style = {
                .size = { .width = Shrink() },
                .margin = { 4_px, 4_px, 4_px, 4_px },
                .padding = { 10_px, 10_px, 10_px, 10_px },
                .border = { .radius = 10_px },
                .background { .color = Color(1, 1, 1, 0.1) },
            };

                for (size_t i = 0; i < 100000; i++) {
                    Element* testBoxChild = new Element(testBox2);
                    testBoxChild->style = &textBoxChildStyle;
                }*/
        }

        // Destroy
        ~Interface() {

        }
    };
};