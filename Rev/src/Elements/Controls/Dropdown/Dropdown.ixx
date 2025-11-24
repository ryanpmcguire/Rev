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
            .layout = { Axis::Vertical, Align::Start, Align::Center, Wrap::False },
            .size = { Grow() },
            .margin = { 4_px, 4_px, 4_px, 4_px }
        };

            Style Label = {
                .margin = { .bottom = 4_px },
                .text = { .color = rgba(0, 0, 0, 0.6), .size = 12_px }
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
                .visibility = { Visibility::Hidden },
                .overflow = Overflow::Hide,
                .layout = { .direction = Axis::Vertical, .vertical = Align::End, .wrap = Wrap::False, .position = Position::Absolute },
                .position = { .top = 100_pct },
                .size = { .width = Grow(), .max = { .width = 100_pct } },
                .margin = { .top = 8_px },
                .background = { .color = rgb(225, 228, 238) },
                .border = { .color = rgb(226, 228, 238), .radius = 6_px },
                .shadow = subtleShadow,
                .zIndex = +2,
            };

                Style Option {
                    .size = { 100_pct },
                    .padding = { 8_px, 8_px, 6_px, 6_px },
                    .background = { .color = rgba(203, 213, 223, 0.0), .transition = 100_ms },
                    .text = { .color = rgba(0, 0, 0, 1), .size = 14_px },
                    .cursor = Cursor::Hand
                };

                    Style OptionHover = {
                        .applies = { .hover = true, .focus = true },
                        .background = { .color = rgba(203, 213, 223, 1.0) }
                    };
                    
                    Style OptionDisabled = {
                        .applies = { .disabled = true },
                        .background = { .color = rgba(203, 213, 223, 0.0) },
                        .text = { .color = rgba(0, 0, 0, 0.667 ) },
                        .cursor = Cursor::Default
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
        Dropdown(Element* parent, Params p = Params::Default(), StyleList styles = {}) : Box(parent, styles) {

            // Self
            this->name = "Dropdown";
            this->params = p;
            this->styles.add(&Styles::Self);

            //this->params.value = { "Select... ", "null" };

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
            dropdownText->content = option.name;

            this->closeMenu();
        }

        void openMenu() {
            dropdownArrow->transition(&dropdownArrow->rotation, 3.14159/2.0f, 200);
            optionsContainer->style->visibility = Visibility::Visible;
            open = true;
        }

        void closeMenu() {
            dropdownArrow->transition(&dropdownArrow->rotation, 3.14158/2.0f + 3.15159, 200);
            optionsContainer->style->visibility = Visibility::Hidden;
            open = false;
        }

        void computeChildren(Event& e) override {

            //if (savedValue == params.value) { return; }

            dropdownText->content = getOptionWithVal(params.value).name;

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
                options[i] = new Text(optionsContainer, option.name, { &Styles::Option, &Styles::OptionHover, &Styles::OptionDisabled });

                options[i]->onMouseDown([this, option](Event& e) {
                    if (option.disabled) { return; }
                    this->select(option);
                });
            }
            
            // Compute content
            for (size_t i = 0; i < newSize; i++) {

                Option& option = params.options[i];
                options[i]->content = option.name;

                if (options[i]->resolved.disabled != option.disabled) {
                    options[i]->resolved.disabled = option.disabled;
                    options[i]->dirty.style = true;
                }
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