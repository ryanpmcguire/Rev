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

            Style Container = {
                .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False },
                .size = { Grow() },
                .padding = { 8_px, 6_px, 6_px, 8_px },
                .background = { rgb(225, 228, 238 )},
                .border = { .radius = 4_px },
                .shadow = subtleShadow
            };

                Style ContainerFocus = {
                    .applies = { .focus = true },
                    .border = { .color = rgb(0, 0, 0), .width = 1_px }
                };

                Style Field = {
                    .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False },
                    .size = { Grow() }
                };

                Style Text = {
                    .size = { Grow() },
                    .text = { .color = rgba(0, 0, 0, 1.0), .size = 12_px, .wrap = Wrap::BreakWord }
                };

                Style Placeholder = {
                    .layout = { .position = Position::Absolute },
                    .position = { .left = 0_px, .top = 0_px },
                    .size = { 100_pct, 100_pct },
                    .text = { .color = rgba(0, 0, 0, 0.6), .size = 12_px }
                };
    };

    using namespace TextInputStyle;

    struct TextInput : public Element {

        struct Params {

            std::string label;
            std::string placeholder;
            size_t maxLength;
            bool selectAllOnFocus = true;

            static Params Default() {
                return {
                    .label = "Text Input",
                    .placeholder = "Enter text...",
                    .maxLength = 500,
                    .selectAllOnFocus = true
                };
            };
        };

        Params params;
        Observable<bool> value;

        Text* label = nullptr;

        Box* container = nullptr;
            Box* field = nullptr;
            Text* placeholderText = nullptr;
            Text* text = nullptr;

        // Create
        TextInput(Element* parent, Params p = Params::Default(), StyleList styles = {}) : Element(parent, styles) {

            this->name = "TextInput";
            this->styles.add(&Styles::Self);
            
            this->params = p;

            label = new Text(this, params.label, { &Styles::Label });

            container = new Box(this, { &Styles::Container, &Styles::ContainerFocus });

            field = new Box(container, { &Styles::Field });

                placeholderText = new Text(field, params.placeholder, { &Styles::Placeholder });

                text = new Text(field, "", { &Styles::Text });
                text->editable = true;
                text->selectable = true;
                text->selectAllOnFocus = params.selectAllOnFocus;
        }

        void computeStyle(Event& e) override {

            bool textFocused = text->targetFlags.focus;

            if (container->targetFlags.focus != textFocused) {
                container->targetFlags.focus = textFocused;
                container->dirty.style = true;
            }

            bool empty = text->content.get().empty();

            Visibility visibility = empty
                ? Visibility::Visible
                : Visibility::Hidden;

            if (placeholderText->style->visibility != visibility) {
                placeholderText->style->visibility = visibility;
                placeholderText->dirty.style = true;
            }

            Element::computeStyle(e);
        }
    };
};
