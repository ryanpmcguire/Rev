module;

#include <string>
#include <functional>

export module Rev.Element.Button;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.ControlTheme;

export namespace Rev::Element {
    struct Button : public Box {
        enum class Variant { Primary, Secondary };

        struct Params {
            std::string label = "Button";
            Variant variant = Variant::Secondary;

            static Params Primary(const std::string& label) { return { .label = label, .variant = Variant::Primary }; }
            static Params Secondary(const std::string& label) { return { .label = label, .variant = Variant::Secondary }; }
        };

        Text* labelText = nullptr;

        Button(Element* parent, Params params, StyleList styles = {}) : Box(parent, styles) {
            name = "Button";

            if (params.variant == Variant::Primary) {
                this->styles.add(&ControlTheme::ButtonPrimary);
                this->styles.add(&ControlTheme::ButtonPrimaryHover);
            } else {
                this->styles.add(&ControlTheme::ButtonSecondary);
                this->styles.add(&ControlTheme::ButtonSecondaryHover);
            }

            Style* labelStyle = params.variant == Variant::Primary ? &ControlTheme::ButtonPrimaryLabel : &ControlTheme::ButtonSecondaryLabel;
            labelText = new Text(this, params.label, { labelStyle });

            onKeyDown([this](Event& e) { click(e); e.propagate = false; });
        }

        void computeStyle(Event& e) override { Box::computeStyle(e); }
    };
}