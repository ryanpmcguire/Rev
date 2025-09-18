module;

export module Interface;

import Rev.Style;
import Rev.Element;
import Rev.Box;
import Rev.TextBox;

import Rev.Slider;

import Resources.Fonts.Arial.Arial_ttf;

export namespace HelloWorld {

    using namespace Rev;

    struct Interface : public Box {

        // Create
        Interface(Element* parent) : Box(parent) {

            // Self
            this->style->alignment = { Axis::Vertical, Align::Center, Align::Center };
            this->style->background.color = rgba(25, 25, 25, 1.0);
            this->style->size = { .width = 100_pct, .height = 100_pct };
            this->style->padding = { 40_px, 40_px, 40_px, 40_px };

            // Grey box
            Box* greyBox = new Box(this, "GreyBox");
            greyBox->style = {
                .size = { .width = Shrink(), .maxWidth = 600_px, .maxHeight = 400_px },
                .padding = { 10_px, 10_px, 10_px, 10_px },
                .border = { .radius = 10_px },
                .background { .color = rgba(255, 255, 255, 0.1) },
            };

            for (size_t i = 0; i < 10; i++) {
                Slider* slider = new Slider(greyBox);
            }

            //Slider* sliderA = new Slider(greyBox, { .val = 0.0 });
            //Slider* sliderB = new Slider(greyBox, { .val = 1.0 });

            /*sliderA->style.size.minWidth = 100_px;
            sliderB->style.size.minWidth = 50_px;*/

            /*TextBox* textBox = new TextBox(greyBox);
            textBox->style = {
                .text = {
                    .font = Arial_ttf,
                    .size = 20_px
                }
            };*/

            /*Box* testBox = new Box(greyBox, "TestBox");
            testBox->style = {
                .size = { },
                .margin = { 4_px, 4_px, 4_px, 4_px },
                .padding = { 10_px, 10_px, 10_px, 10_px },
                .border = { .radius = 10_px },
                .background { .color = Color(1, 1, 1, 0.1) },
            };

                static Style childStyle = {
                    .size = { 10_px, 10_px },
                    .margin = { 4_px, 4_px, 4_px, 4_px },
                    .background = { .color = Color(1, 0.5, 0.5, 1.0) }
                };

                for (size_t i = 0; i < 10000; i++) {
                    Box* testBoxChild = new Box(testBox);
                    testBoxChild->styles = { &childStyle };
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