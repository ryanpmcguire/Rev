module;

#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

#include <managed.hpp>

export module Rev.Element.Dropdown;

import Rev.Core.Pos;
import Rev.Core.Resource;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Svg;

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
                .alignment = { Axis::Horizontal, Align::Center, Align::Center },
                .size = { Grow() },
                .padding = { 8_px, 6_px, 6_px, 8_px },
                .background = { rgb(225, 228, 238 )},
                .border = { .radius = 6_px },
                .shadow = subtleShadow
            };

                Style DropdownFocus {
                    .applies = { .focus = true },
                    .border = { .color = rgb(64, 77, 255), .width = 2_px }
                };

                Style DropdownText {
                    .size = { Grow() },
                    .text = { .color = rgba(0, 0, 0, 0.8), .size = 14_px }
                };

                Style DropdownArrow = {
                    .size = { 14_px, 14_px },
                    .background = { .color = rgba(0, 0, 0, 1.0 ) }
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
                        .applies = { .hover = true, .focus = true },
                        .background = { .color = rgba(203, 213, 223, 1.0) }
                    };
    };

    using namespace DropdownStyle;

    struct Dropdown : public Box {

        // Label text
        Text* label = nullptr;

        Box* dropdown = nullptr;
            Text* dropdownText = nullptr;
            Svg* dropdownArrow = nullptr; 

        // Option elements
        Box* optionsContainer = nullptr;
        std::vector<Text*> options;

        struct Params {

            // Options: a list of options to choose from
            struct Option { std::string name; std::string value; };

            std::vector<Option> options;
            Option value;

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

            dropdown = new Box(this, { &Styles::Dropdown, &Styles::DropdownFocus });
                dropdownText = new Text(dropdown, "Option", { &Styles::DropdownText });
                dropdownArrow = new Svg(dropdown, File("./chevron-right.svg"), { &Styles::DropdownArrow });

                // Options container
                //--------------------------------------------------

                optionsContainer = new Box(dropdown, { &Styles::OptionsContainer });
                optionsContainer->name = "OptionsContainer";

            // Events
            //--------------------------------------------------

            dropdown->onLoseFocus([this](Event& e) {
                optionsContainer->style->visibility = Visibility::Hidden;
                optionsContainer->style->dirty = true;
            });

            dropdown->onGainFocus([this](Event& e) {
                optionsContainer->style->visibility = Visibility::Visible;
                optionsContainer->style->dirty = true;
            });
        }

        std::string savedValue = "";

        void select(Params::Option option) {
            
            params.value = option;
            dropdownText->content = params.value.name;

            optionsContainer->style->visibility = Visibility::Hidden;
            optionsContainer->style->dirty = true;
        }

        void computeChildren(Event& e) override {

            //if (savedValue == params.value) { return; }

            dropdownText->content = params.value.name;

            size_t oldSize = options.size();
            size_t newSize = params.options.size();

            // Delete old
            for (size_t i = newSize; i < oldSize; i++) { delete options[i]; }
            options.resize(newSize);

            // Add new
            for (size_t i = oldSize; i < newSize; i++) {

                Params::Option option = params.options[i];
                options[i] = new Text(optionsContainer, option.name, { &Styles::Option, &Styles::OptionHover });

                options[i]->onMouseDown([this, option](Event& e) {
                    this->select(option);
                });
            }
            
            // Compute content
            for (size_t i = 0; i < newSize; i++) {
                Params::Option option = params.options[i];
                options[i]->content = option.name;
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