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
            .layout = { Axis::Vertical, Align::Start, Align::Center },
            .size = { Grow() },
            .margin = { 4_px, 4_px, 4_px, 4_px }
        };

            Style Label = {
                .margin = { .bottom = 4_px },
                .text = { .size = 12_px, .color = rgba(0, 0, 0, 0.6) }
            };

            Style Dropdown {
                .layout = { Axis::Horizontal, Align::Center, Align::Center },
                .size = { Grow() },
                .padding = { 8_px, 6_px, 6_px, 8_px },
                .background = { rgb(225, 228, 238 )},
                .border = { .radius = 6_px },
                .shadow = subtleShadow
            };

                Style DropdownFocus {
                    .applies = { .focus = true },
                    .border = { .color = rgb(109, 119, 255), .width = 1_px }
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
                .visibility = Visibility::Hidden,
                .overflow = Overflow::Hide,
                .layout = { .direction = Axis::Vertical, .vertical = Align::End, .position = Position::Absolute, .wrap = Wrap::False },
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
            this->params.value = { "Select... ", "null" };

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
                this->closeMenu();
            });

            dropdown->onGainFocus([this](Event& e) {
                if (!this->open) { this->openMenu(); }
            });

            dropdown->onMouseDown([this](Event& e) {
                if (this->open) { this->closeMenu(); }
                else { this->openMenu(); }
            });
        }

        std::string savedValue = "";
        bool open = false;
        
        void select(Params::Option option) {
            
            params.value = option;
            dropdownText->content = params.value.name;

            this->closeMenu();
        }

        void openMenu() {
            dropdownArrow->transition(&dropdownArrow->rotation, 3.14159/2.0f, 200);
            optionsContainer->style->visibility = Visibility::Visible;
            optionsContainer->style->size.height = 400_px;
            open = true;
        }

        void closeMenu() {
            dropdownArrow->transition(&dropdownArrow->rotation, 3.14158/2.0f + 3.15159, 200);
            optionsContainer->style->visibility = Visibility::Visible;
            
            optionsContainer->style->size.height = 0_px;
            optionsContainer->style->size.transition = 1000_ms;
            open = false;
        }

        void computeChildren(Event& e) override {

            //if (savedValue == params.value) { return; }

            dropdownText->content = params.value.name;

            size_t oldSize = options.size();
            size_t newSize = params.options.size();

            // Delete old
            for (size_t i = newSize; i < oldSize; i++) {
                delete options[i];
            }

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