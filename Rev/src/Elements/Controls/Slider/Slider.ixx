module;

#include <cmath>
#include <string>
#include <algorithm>

export module Rev.Element.Slider;

import Rev.Core.Pos;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.ControlTheme;

export namespace Rev::Element {

    using namespace ControlTheme;

    namespace SliderStyle {

        Style Self = {
            .size = { .width = Grow(), .min = { .width = 100_px } },
            .margin = { 0_px, 0_px, 12_px, 0_px }
        };

        Style TextRow = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center },
            .size = { 100_pct },
            .margin = { 0_px, 0_px, 6_px, 0_px }
        };

    }

    struct Slider : public Element {

        Element* textContainer = nullptr;
            Text* labelText = nullptr;
            Text* valueText = nullptr;

        Box* sliderContainer = nullptr;
            Box* track = nullptr;
            Element* thumbContainer = nullptr;
                Box* thumb = nullptr;

        struct SliderData {

            float min = 0;
            float max = 1000;
            float def = 0.5;
            float val = 1.0;

            SliderData() {}
        };

        SliderData data;

        Slider(
            Element* parent,
            SliderData sliderData = SliderData(),
            StyleList styles = {},
            std::string name = "Slider"
        ) : Element(parent, styles, name) {

            this->data = sliderData;
            this->styles.add(&SliderStyle::Self);

            textContainer = new Element(this, { &SliderStyle::TextRow });
                
                labelText = new Text(textContainer, "Value: ", { &Label });
                valueText = new Text(textContainer, "", { &SliderValueText });
                valueText->setContent(data.val);
                    
            sliderContainer = new Box(
                this,
                { &Field, &FieldFocus, &SliderFieldHover },
                "SliderContainer"
            );

            track = new Box(sliderContainer, { &SliderTrack }, "Track");
                thumbContainer = new Element(track, { &FieldInner }, "Container");
                    thumb = new Box(
                        thumbContainer,
                        { &SliderThumb, &SliderThumbHover },
                        "Thumb"
                    );

            sliderContainer->onMouseDown([this] (Event& e) {
                float newVal = posToVal(e.mouse.pos);
                if (setVal(newVal)) { refresh(e); }
            });

            sliderContainer->onDrag([this] (Event& e) {
                float newVal = posToVal(e.mouse.pos);
                if (setVal(newVal)) { refresh(e); }
            });
        }

        bool setVal(float newVal) {

            float clamped = std::clamp(newVal, data.min, data.max);
            if (clamped == data.val) { return false; }
            else { data.val = clamped; valueText->setContent(data.val); return true; }
        }

        float posToVal(Pos& pos) {
            return (data.max - data.min) * track->rect.posWithin(pos).x;
        }

        void computeStyle(Event& e) override {

            float pctVal = (data.val - data.min) / (data.max - data.min);
            thumbContainer->style->position.left = Pct(100.0f * pctVal);
        
            Element::computeStyle(e);
        }
    };
};
