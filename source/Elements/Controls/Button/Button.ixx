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

            // Optional caller styles for the label Text. Applied AFTER the
            // theme's label style so the caller wins -- the same ordering
            // contract the box styles follow.
            StyleList labelStyles = {};

            static Params Primary(const std::string& label) { return { .label = label, .variant = Variant::Primary }; }
            static Params Secondary(const std::string& label) { return { .label = label, .variant = Variant::Secondary }; }
        };

        Text* labelText = nullptr;

        // A disabled button renders its *Disabled styles (applies.disabled) and
        // ignores clicks/keys. Display-only, like a locked TextInput.
        bool disabled = false;

        Button(Element* parent, Params params, StyleList styles = {}) : Box(parent, styles) {
            name = "Button";

            // The control's own theme styles are the DEFAULT: they must sit
            // ahead of the caller-supplied styles so anything passed in the
            // constructor overrides them. Prepend the pair in reverse (hover,
            // then base) so the final order is [base, hover, ...caller].
            if (params.variant == Variant::Primary) {
                this->styles.prepend(&ControlTheme::ButtonPrimaryHover);
                this->styles.prepend(&ControlTheme::ButtonPrimary);
            } else {
                this->styles.prepend(&ControlTheme::ButtonSecondaryHover);
                this->styles.prepend(&ControlTheme::ButtonSecondary);
            }

            Style* labelStyle = params.variant == Variant::Primary ? &ControlTheme::ButtonPrimaryLabel : &ControlTheme::ButtonSecondaryLabel;
            labelText = new Text(this, params.label, { labelStyle });

            // Caller label styles override the theme default (see Params).
            for (Style* style : params.labelStyles.styles) { labelText->styles.add(style); }

            onKeyDown([this](Event& e) { click(e); e.propagate = false; });
        }

        // Enable / disable the button. The disabled state cascades to the label
        // automatically (see Element::cascadeStyle), so we only set our own
        // intent here. Idempotent, so it is safe to call every frame.
        void setDisabled(bool d) {
            disabled = d;
            Element::setDisabled(d);
        }

        // A disabled button is inert: it dispatches no click to listeners.
        void click(Event& e) override {
            if (disabled) { e.propagate = false; return; }
            Box::click(e);
        }

        void computeStyle(Event& e) override { Box::computeStyle(e); }
    };
}