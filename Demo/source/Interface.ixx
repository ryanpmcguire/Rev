module;

#include <cstddef>
#include <cmath>
#include <string>

export module Interface;

import Rev.Element;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Slider;
import Rev.Element.Dropdown;
import Rev.Element.Radio;
import Rev.Element.Checkbox;
import Rev.Element.TextInput;
import Rev.Element.Chart;

export namespace HelloWorld {

    using namespace Rev;
    using namespace Rev::Element;

    struct Interface : public Box {

        // Create
        Interface(Element* parent) : Box(parent) {

            // Self
            this->style->layout = { Axis::Horizontal, Align::Start, Align::Start };
            this->style->background.color = rgba(0, 0, 0, 0.0);
            this->style->size = { .width = 100_pct, .height = 100_pct };
            this->style->padding = { 100_px, 100_px, 100_px, 100_px };

            Box* greyBox = new Box(this, {}, "GreyBox");
            greyBox->style = {
                .layout = { Axis::Horizontal, Align::Start, Align::Start },
                .size = { .width = Grow(), .height = Grow(), .max = { .width = 100_pct } },
                .margin = { 5_px, 5_px, 5_px, 5_px },
                .padding = { 10_px, 10_px, 10_px, 10_px },
                .background { .color = rgba(0, 0, 0, 0.05) },
                .border = { .radius = 10_px },
                .shadow = { .color = rgba(0, 0, 0, 0.5), .size = Px(-10), .blur = 20_px }
            };

                //Box* holder = new Box(greyBox);
                for (size_t i = 0; i < 1; i++) {
                    Text* testText = new Text(greyBox, "Hello_World");
                    testText->editable = true;
                    testText->style->text.size = 32_px;
                    testText->style->text.color = rgba(0, 0, 0, 1);
                    testText->style->background.color = rgba(1, 0, 0, 0.2);
                    testText->style->text.wrap = Wrap::BreakWord;
                }

                std::string harryPotter = "Mr. and Mrs. Dursley, of number four, Privet Drive, were proud to say that they were perfectly normal, thank you very much.";
                Text* testWrap = new Text(greyBox, harryPotter);
                testWrap->style->text.wrap = Wrap::BreakWord;
                testWrap->selectable = true;

                Dropdown* dropdown = new Dropdown(greyBox, {
                    .options = { { "Option A", "0" }, { "Option B", "2 "}, { "Option C", "3", true} },
                    .placeholder = "A or B..."
                });

                Slider* slider = new Slider(greyBox);

                Radio* radio = new Radio(greyBox, {
                    .options = { { "Pizza", "0" }, { "Hamburger", "2 "}, { "Option C", "3", true} }
                });

                Checkbox* checkbox = new Checkbox(greyBox);

                TextInput* textInput = new TextInput(greyBox);

                /*Chart* chart = new Chart(greyBox);
                
                chart->style = {
                    .size = { .width = 100_pct, .height = Grow(), .min = { .height = 100_px } },
                    .border = { .color = rgba(0, 0, 0, 0.1), .radius = 100_px, .width = 1_px, .bottom = { .color = rgba(255, 0, 0, 1), .width = 50_px }, .transition = 200 }
                };

                size_t num = 100;
                for (size_t i = 0; i < num; i++) {
                    float t = float(i) / float(num);
                    chart->points.push_back({ t, 0.5f + 0.5f * sin(10.0f * 3.14159f * t) });
                }*/
        }

        // Destroy
        ~Interface() {

        }
    };
};