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

        Shadow subtleShadow = {
            .color = rgba(0, 0, 0, 0.5),
            .size = Px(-10), .blur = 20_px
        };
        
        Style Self = {
            .alignment = { Axis::Vertical, Align::Start, Align::Center },
            .size = { Grow() },
            .margin = { 4_px, 4_px, 4_px, 4_px }
        };

            Style Label = {
                .margin = { .bottom = 4_px },
                .text = { .size = 12_px, .color = rgba(0, 0, 0, 0.6) }
            };

            Style Dropdown {
                .size = { Grow() },
                .padding = { 8_px, 6_px, 6_px, 8_px },
                .background = { rgb(225, 228, 238 )},
                .border = { .radius = 6_px },
                .shadow = subtleShadow
            };

                Style DropdownText {
                    .size = { Grow() },
                    .text = { .color = rgba(0, 0, 0, 0.8), .size = 14_px }
                };

            Style OptionsContainer {
                .visibility = Visibility::Visible,
                .overflow = Overflow::Hide,
                .alignment = { .direction = Axis::Vertical, .position = Position::Absolute },
                .position = { .top = 100_pct },
                .size = { Grow(), .max = { 100_pct } },
                .margin = { .top = 8_px },
                .background = { .color = rgb(225, 228, 238) },
                .border = { .color = rgb(226, 228, 238), .radius = 6_px },
                .shadow = subtleShadow,
            };

                Style Option {
                    .size = { 100_pct },
                    .padding = { 8_px, 8_px, 6_px, 6_px },
                    .text = { .color = rgba(0, 0, 0, 1), .size = 14_px },
                    .background = { .color = rgba(203, 213, 223, 0.0), .transition = 100_ms },
                    .cursor = Cursor::Hand
                };

                    Style OptionHover = {
                        .applies = { .hover = true },
                        .background = { .color = rgba(203, 213, 223, 1.0) }
                    };
    };

    using namespace DropdownStyle;

    struct Dropdown : public Box {

        // Label text
        Text* label = nullptr;

        Box* dropdown = nullptr;
            Box* dropdownText = nullptr;

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
            this->styles.add(&Styles::Self);

            this->params.options = { { "Option 1", "1" }, { "Option 2", "2"}, { "Option Option Option", "3" } };

            // Label
            label = new Text(this, "Dropdown", { &Styles::Label });

            // Dropdown per-se
            //--------------------------------------------------

            dropdown = new Box(this, { &Styles::Dropdown });
                dropdownText = new Text(dropdown, "Option", { &Styles::DropdownText });

            // Options container
            //--------------------------------------------------

            optionsContainer = new Box(this, { &Styles::OptionsContainer });
            optionsContainer->name = "OptionsContainer";

            for (Params::Option& option : params.options) {
                options.push_back(new Text(optionsContainer, option.name, { &Styles::Option, &Styles::OptionHover }));
            }
        }

        std::string savedValue = "";

        void computeChildren(Event& e) override {

            //if (savedValue == params.value) { return; }

            
        }

        void computeStyle(Event& e) override {
        
            Box::computeStyle(e);
        }

        void computePrimitives(Event& e) override {

            Box::computePrimitives(e);
        }
    };
};