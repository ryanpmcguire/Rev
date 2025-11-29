module;

#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

#include <managed.hpp>

export module Rev.Element.Checkbox;

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

    namespace CheckboxStyle::Styles {

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

            Style Checkbox {
                .layout = { Axis::Horizontal, Align::Center, Align::Center },
                .size = { },
                .padding = { 2_px, 2_px, 2_px, 2_px },
                .background = { rgb(225, 228, 238 )},
                .border = { .radius = 2_px },
                .shadow = subtleShadow
            };

                Style CheckboxFocus {
                    .applies = { .focus = true },
                    .border = { .color = rgb(0, 0, 0), .width = 1_px }
                };

                Style CheckboxChecked {
                    .background = { .color = rgb(43, 91, 224) }
                };

                Style CheckboxPress {
                    .applies = { .press = true },
                    .background = { .color = rgb(31, 65, 160) }
                };

                Style Check = {
                    .size = { 14_px, 14_px },
                    .text = { .color = rgba(255, 255, 255, 1.0 ) }
                };
    };

    using namespace CheckboxStyle;

    struct Checkbox : public Element {

        // Label text
        Text* label = nullptr;

        Box* checkbox = nullptr;
            Svg* check = nullptr; 

        struct Params {

            std::string label;
            bool def;

            static Params Default() {
                return {
                    .label = "checkbox",
                    .def = false
                };
            };
        };

        Params params;
        Observable<bool> value;

        // Create
        Checkbox(Element* parent, Params p = Params::Default(), StyleList styles = {}) : Element(parent, styles) {

            // Self
            this->name = "Checkbox";
            this->styles.add(&Styles::Self);
            
            this->params = p;
            this->value = params.def;

            // Label
            label = new Text(this, params.label, { &Styles::Label });

            // Dropdown per-se
            //--------------------------------------------------

            checkbox = new Box(this, { &Styles::Checkbox, &Styles::CheckboxFocus });
                check = new Svg(checkbox, File("./check.svg"), { &Styles::Check });

            // Events
            //--------------------------------------------------

            checkbox->onClick([this](Event& e) {

                if (this->resolved.disabled) { return; }
                this->value = !value;

                this->refresh(e);
            });
        }

        void computeStyle(Event& e) {

            if (value.changed()) {
                
                if (value) {

                    checkbox->styles.add(&Styles::CheckboxChecked);
                    checkbox->styles.add(&Styles::CheckboxPress);
                    
                    check->opacity = 1.0f;
                }

                else {

                    checkbox->styles.remove(&Styles::CheckboxChecked);
                    checkbox->styles.remove(&Styles::CheckboxPress);

                    check->opacity = 0.0f;
                }

                value.changedFlag = false;
            }

            Element::computeStyle(e);
        }
    };
};