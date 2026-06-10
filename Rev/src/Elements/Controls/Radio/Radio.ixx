module;

#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

#include <managed.hpp>

export module Rev.Element.Radio;

import Rev.Core.Pos;
import Rev.Core.Resource;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Svg;

export namespace Rev::Element {

    namespace RadioStyle::Styles {

        Shadow subtleShadow = {
            .color = rgba(0, 0, 0, 0.2),
            .size = Px(0), .blur = 5_px
        };
        
        Style Self = {
            .layout = { Axis::Vertical, Align::Start, Align::Center, Wrap::False },
            //.size = { Grow() },
            .margin = { 4_px, 4_px, 4_px, 4_px }
        };

            Style Label = {
                .margin = { .bottom = 4_px },
                .text = { .color = rgba(0, 0, 0, 0.6), .size = 12_px }
            };

            Style Option {
                .size = { 100_pct },
                .padding = { 8_px, 8_px, 6_px, 6_px },
                .background = { .color = rgba(203, 213, 223, 0.0), .transition = 100_ms },
                .text = { .color = rgba(0, 0, 0, 1), .size = 14_px }
            };

                Style OptionLabel = {
                    .text = { .color = rgba(0, 0, 0, 1), .size = 14_px }
                };

                Style OptionDot = {
                    .size = { 14_px, 14_px },
                    .margin = { .right = 6_px },
                    .background = { .color = rgba(0, 0, 0, 0) },
                    .border = { .color = rgba(0, 0, 0, 0.667), .radius = 100_px, .width = 1_px, },
                    .cursor = Cursor::Hand
                    //.shadow = subtleShadow
                };

                    Style OptionDotDisabled = {
                        .applies = { .disabled = true },
                        .cursor = Cursor::Default
                    };
    };

    using namespace RadioStyle;

    struct RadioOption : public Element {

        Box* dot = nullptr;
        Text* label = nullptr;

        RadioOption(Element* parent, StyleList styles = {}) : Element(parent, styles) {

            dot = new Box(this, { &Styles::OptionDot });
            label = new Text(this, "Option", { &Styles::OptionLabel });
        };
    };

    struct Radio : public Box {

        // Label text
        Text* label = nullptr;

        // Option elements
        std::vector<RadioOption*> options;
        bool open = false;

        struct Option { std::string name; std::string value; bool disabled; };
        
        struct Params {

            std::vector<Option> options;
            std::string placeholder;
            std::string value;

            static Params Default() {
                return {
                    .options = { { "Select...", "" }, { "Option 1", "1" }, { "Option 2", "2" }, { "Disabled", "3", true } },
                    .placeholder = "Select...",
                    .value = "",
                };
            };
        };

        Params params;

        // Create
        Radio(Element* parent, Params p = Params::Default(), StyleList styles = {}) : Box(parent, styles) {

            // Self
            this->name = "Radio";
            this->params = p;
            this->styles.add(&Styles::Self);

            //this->params.value = { "Select... ", "null" };

            // Label
            label = new Text(this, "Radio", { &Styles::Label });
        }

        Option getOptionWithVal(std::string val) {

            for (Option& option : params.options) {
                if (option.value == val) {
                    return option;
                }
            }

            return { params.placeholder, "" };
        }

        Option getOptionWithName(std::string name) {

            for (Option& option : params.options) {
                if (option.name == name) {
                    return option;
                }
            }

            return { params.placeholder, "" };
        }
        
        void select(Option option) {
        
            params.value = option.value;
        }

        void computeChildren(Event& e) override {

            //if (savedValue == params.value) { return; }

            size_t oldSize = options.size();
            size_t newSize = params.options.size();

            // Delete old
            for (size_t i = newSize; i < oldSize; i++) {
                delete options[i];
            }

            options.resize(newSize);

            // Add new
            for (size_t i = oldSize; i < newSize; i++) {

                Option& option = params.options[i];
                options[i] = new RadioOption(this, { &Styles::Option });

                options[i]->dot->onMouseDown([this, option](Event& e) {
                    if (option.disabled) { return; }
                    this->select(option);
                });
            }
            
            // Compute content
            for (size_t i = 0; i < newSize; i++) {

                Option& option = params.options[i];
                RadioOption* optionElem = options[i];

                optionElem->resolved.disabled = option.disabled;
                optionElem->label->content = option.name;

                Color backgroundColor = option.value == params.value ? rgb(51, 106, 255) : Color::Null();
                Color textColor = option.disabled ? rgba(0, 0, 0, 0.5) : Color::Null();

                optionElem->dot->style->background.color = backgroundColor;
                optionElem->label->style->text.color = textColor;
            }
        }

        void computeStyle(Event& e) override {


        
            Box::computeStyle(e);
        }

        void computePrimitives(Event& e) override {

            Box::computePrimitives(e);
        }
    };
};