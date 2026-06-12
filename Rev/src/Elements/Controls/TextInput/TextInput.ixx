module;

#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

#include <managed.hpp>

export module Rev.Element.TextInput;

import Rev.Core.Pos;
import Rev.Core.Resource;
import Rev.Core.Observable;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Svg;

export namespace Rev::Element {

    namespace TextInputStyle::Styles {

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

            Style Container {
                .layout = { Axis::Horizontal, Align::Center, Align::Center },
                .size = { Grow() },
                .padding = { 8_px, 6_px, 6_px, 8_px },
                .background = { rgb(225, 228, 238 )},
                .border = { .radius = 4_px },
                .shadow = subtleShadow
            };

                Style ContainerFocus {
                    .applies = { .focus = true },
                    .border = { .color = rgb(0, 0, 0), .width = 1_px }
                };

                Style Text {
                    .size = { .min = { 100_pct }},
                    .text = { .color = rgba(0, 0, 0, 1.0), .size = 12_px, .wrap = Wrap::BreakWord }
                };

                Style Placeholder {
                    .text = { .color = rgba(0, 0, 0, 0.6), .size = 12_px }      
                };
    };

    using namespace TextInputStyle;

    struct TextInput : public Element {

        struct Params {

            std::string label;
            std::string placeholder;
            size_t maxLength;

            static Params Default() {
                return {
                    .label = "Text Input",
                    .placeholder = "Enter text...",
                    .maxLength = 500
                };
            };
        };

        Params params;
        Observable<bool> value;

        // Label text
        Text* label = nullptr;

        Box* container = nullptr;
            Text* text = nullptr;
            Text* placeholder = nullptr;

        // Create
        TextInput(Element* parent, Params p = Params::Default(), StyleList styles = {}) : Element(parent, styles) {

            // Self
            this->name = "Checkbox";
            this->styles.add(&Styles::Self);
            
            this->params = p;

            // Label
            label = new Text(this, params.label, { &Styles::Label });

            // Container for text and placeholder
            container = new Box(this, { &Styles::Container, &Styles::ContainerFocus });

                placeholder = new Text(container, params.placeholder, { &Styles::Placeholder });

                text = new Text(container, "", { &Styles::Text });
                text->editable = true;
        }

        void computeStyle(Event& e) override {
            if (placeholder && text) {
                bool hasText = !text->content.get().empty();
                placeholder->style->size.width = hasText ? Px(0) : Dist{};
                placeholder->style->overflow   = hasText ? Overflow::Hide : Overflow::Show;
            }
        }
    };
};