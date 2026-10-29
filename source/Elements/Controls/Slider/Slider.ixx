module;

#include <cmath>
#include <string>
#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

export module Rev.Element.Slider;

import Rev.Core.Pos;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.ControlTheme;

export namespace Rev::Element {

    using namespace ControlTheme;

    namespace SliderStyle {

        Style Self = {
            .size = { .width = Grow(), .min = { .width = 100_px } },
            .margin = { .bottom = 12_px }
        };

        Style TextRow = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center },
            .size = { 100_pct },
            .margin = { .bottom = 6_px }
        };

    }

    struct Slider : public Element {

        struct Theme {
            StyleList root;
            StyleList textRow;
            StyleList label;
            StyleList value;
            StyleList field;
            StyleList track;
            StyleList fill;
            StyleList thumbContainer;
            StyleList thumb;
        };

        static Theme DefaultTheme() {
            return {
                .root = {&SliderStyle::Self},
                .textRow = {&SliderStyle::TextRow},
                .label = {&Label},
                .value = {&SliderValueText},
                .field = {&Field, &FieldFocus, &SliderFieldHover},
                .track = {&SliderTrack},
                .fill = {&SliderFill},
                .thumbContainer = {&FieldInner},
                .thumb = {&SliderThumb, &SliderThumbHover}
            };
        }

        Element* textContainer = nullptr;
            Text* labelText = nullptr;
            Text* valueText = nullptr;

        Box* sliderContainer = nullptr;
            Box* track = nullptr;
            Box* fill = nullptr;
            Element* thumbContainer = nullptr;
                Box* thumb = nullptr;

        struct SliderData {

            float min = 0;
            float max = 1000;
            float step = 0;
            float def = 0.5;
            float val = 1.0;

            SliderData() {}
        };

        SliderData data;
        std::vector<Theme> themes;
        std::function<void(Event&, float)> onValueChange;
        std::function<void(Event&, float)> onValueCommit;
        bool interactionChanged = false;

        Slider(
            Element* parent,
            SliderData sliderData,
            std::vector<Theme> requestedThemes,
            StyleList styles = {},
            std::string name = "Slider"
        ) : Element(parent) {

            this->name = std::move(name);
            this->data = sliderData;
            themes.push_back(DefaultTheme());

            for (Theme& requestedTheme : requestedThemes) {
                themes.push_back(std::move(requestedTheme));
            }

            append(this, &Theme::root);
            append(this, styles);

            textContainer = new Element(this);
            append(textContainer, &Theme::textRow);
                
                labelText = new Text(textContainer, "Value: ");
                append(labelText, &Theme::label);
                valueText = new Text(textContainer, "");
                append(valueText, &Theme::value);
                valueText->setContent(data.val);
                    
            sliderContainer = new Box(this, {}, "SliderContainer");
            append(sliderContainer, &Theme::field);

            track = new Box(sliderContainer, {}, "Track");
            append(track, &Theme::track);
                fill = new Box(track, {}, "Fill");
                append(fill, &Theme::fill);
                thumbContainer = new Element(track, {}, "Container");
                append(thumbContainer, &Theme::thumbContainer);
                    thumb = new Box(thumbContainer, {}, "Thumb");
                    append(thumb, &Theme::thumb);

            sliderContainer->onMouseDown([this] (Event& e) {
                float newVal = posToVal(e.mouse.pos);
                if (setVal(newVal)) {
                    interactionChanged = true;
                    if (onValueChange) { onValueChange(e, data.val); }
                    refresh(e);
                }
            });

            sliderContainer->onDrag([this] (Event& e) {
                float newVal = posToVal(e.mouse.pos);
                if (setVal(newVal)) {
                    interactionChanged = true;
                    if (onValueChange) { onValueChange(e, data.val); }
                    refresh(e);
                }
            });

            sliderContainer->onMouseUp([this] (Event& e) {
                if (!interactionChanged) {
                    return;
                }

                interactionChanged = false;

                if (onValueCommit) {
                    onValueCommit(e, data.val);
                }
            });
        }

        Slider(
            Element* parent,
            SliderData sliderData,
            Theme requestedTheme,
            StyleList styles = {},
            std::string name = "Slider"
        ) : Slider(
            parent,
            sliderData,
            std::vector<Theme>{std::move(requestedTheme)},
            std::move(styles),
            std::move(name)
        ) {}

        Slider(
            Element* parent,
            SliderData sliderData = SliderData(),
            StyleList styles = {},
            std::string name = "Slider"
        ) : Slider(
            parent,
            sliderData,
            std::vector<Theme>{},
            std::move(styles),
            std::move(name)
        ) {}

        static void append(Element* element, const StyleList& styles) {
            for (Style* style : styles.styles) {
                element->styles.add(style);
            }
        }

        void append(Element* element, StyleList Theme::* member) {
            for (Theme& theme : themes) {
                append(element, theme.*member);
            }
        }

        bool setVal(float newVal) {

            float clamped = std::clamp(newVal, data.min, data.max);

            if (data.step > 0) {
                clamped = data.min + std::round(
                    (clamped - data.min) / data.step
                ) * data.step;
                clamped = std::clamp(clamped, data.min, data.max);
            }

            if (clamped == data.val) { return false; }
            else { data.val = clamped; valueText->setContent(data.val); return true; }
        }

        float posToVal(Pos& pos) {
            return data.min + (data.max - data.min) * track->rect.posWithin(pos).x;
        }

        void computeStyle(Event& e) override {

            float range = data.max - data.min;
            float pctVal = range == 0.0f ? 0.0f : (data.val - data.min) / range;
            fill->style->size.width = Pct(100.0f * pctVal);
            thumbContainer->style->position.left = Pct(100.0f * pctVal);
        
            Element::computeStyle(e);
        }
    };
};
