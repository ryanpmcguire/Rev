module;

#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

export module Rev.Element.Dropdown;

import Rev.Core.Pos;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;

export namespace Rev::Element {

    namespace DropdownStyle::Styles {
        
        Style Dropdown = {
            .alignment = { Axis::Vertical, Align::Start, Align::Center },
            .size = { .width = Grow() },
            .margin = { 4_px, 4_px, 4_px, 4_px },
            .padding = { 6_px, 6_px, 6_px, 6_px },
            .background = { .color = rgba(0, 0, 0, 0.1), .transition = 0.1_sec },
            .border = { .radius = 4_px }
        };

            Style Label = {
                .margin = { .bottom = 4_px },
                .text = { .size = 12_px, .color = rgba(0, 0, 0, 1) }
            };

            Style OptionsContainer {
                .visibility = Visibility::Visible,
                .alignment = { Axis::Vertical },
                .background = { .color = rgba(0, 0, 0, 0.05) }
            };

                Style Option {
                    .padding = { .top = 4_px, .bottom = 4_px },
                    .border = { .bottom = { .color = rgba(0, 0, 0, 0.2), .width = 1_px } },
                    .text = { .color = rgba(0, 0, 0, 1), .size = 14_px },
                    .cursor = Cursor::Hand,
                };

                    Style OptionHover = {
                        .applies = { .hover = true },
                        .background = { .color = rgba(0, 0, 0, 0.2) }
                    };
    };

    using namespace DropdownStyle;

    struct Dropdown : public Box {

        // Label text
        Text* label = nullptr;

        // Option elements
        Box* optionsContainer = nullptr;
        std::vector<Text*> options;

        struct Params {

            // Options: a list of options to choose from
            struct Option { std::string name = ""; std::string value = ""; };

            std::vector<Option> options;
            std::string value;

        };

        Params params;

        // Create
        Dropdown(Element* parent, Params p = Params(), StyleList styles = {}) : Box(parent, styles) {

            // Self
            this->name = "Dropdown";
            this->params = p;
            this->styles = { &Styles::Dropdown };

            this->params.options = { { "Option 1", "1" }, { "Option 2", "2"}, { "Option 3", "3" } };

            // Label
            label = new Text(this, "Dropdown", { &Styles::Label });
        }

        std::string savedValue = "";

        void computeChildren(Event& e) override {

            //if (savedValue == params.value) { return; }

            // Delete options container
            if (optionsContainer) { delete optionsContainer; }
            
            // Options container
            //--------------------------------------------------

            optionsContainer = new Box(this, { &Styles::OptionsContainer });
            optionsContainer->name = "OptionsContainer";

            for (Params::Option& option : params.options) {
                options.push_back(new Text(optionsContainer, option.name, { &Styles::Option, &Styles::OptionHover }));
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