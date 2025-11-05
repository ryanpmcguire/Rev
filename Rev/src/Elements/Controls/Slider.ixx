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

export namespace Rev::Element {

    namespace Styles {
        
        Style Self = {
            .size = { .width = Grow(), .minWidth = 100_px },
            .margin = { 4_px, 4_px, 4_px, 4_px },
        };

            Style TextContainer = {
                .size = { 100_pct },
                .margin = { .bottom = 4_px }
            };

                Style LabelText = {
                    .text = { .size = 12_px, .color = rgba(0, 0, 0, 1) }
                };

                Style ValueText = {
                    .text = { .size = 12_px, .color = rgba(0, 0, 0, 1) }
                };

            Style Slider = {
                .alignment = { Axis::Vertical, Align::Start, Align::Center },
                .size = { .width = Grow() },
                .padding = { 6_px, 6_px, 6_px, 6_px },
                .background = { .color = rgba(0, 0, 0, 0.1), .transition = 0.1_sec },
                .border = { .radius = 4_px }
            };

                Style SliderHover = {
                    .applies = { .hover = true },
                    .background = { .color = rgba(0, 0, 0, 0.15) }
                };

            Style Track = {
                .alignment = { Axis::Vertical, Align::Start, Align::Center, Break::True },
                .size = { .width = 100_pct, .height = 2_px, .minWidth = 100_px },
                .background = { .color = rgba(0, 0, 0, 0.25) },
            };

                Style ThumbContainer = {
                    .alignment = { Axis::Vertical, Align::Center, Align::Center },
                    .size = { .width = 0_px, .height = 0_px }
                };

                    Style Thumb = {
                        .size = { .width = 4_px, .height = 8_px, .transition = 100 },
                        .background = { .color = rgba(0, 0, 0, 0.5) },
                    };

                    Style ThumbHover = {
                        .applies = { .hover = true, .drag = true },
                        .size = { .width = 8_px, .height = 16_px }
                    };
    };

    struct Slider : public Box {

        // Text
        Element* textContainer = nullptr;
            Text* labelText = nullptr;
            Text* valueText = nullptr;

        // Slider per-se
        Box* sliderContainer = nullptr;
            Box* track = nullptr;
            Element* thumbContainer = nullptr;
                Box* thumb = nullptr;

        struct SliderData {

            float min = 0;
            float max = 1000;
            float def = 0.5;
            float val = 1.0;

            SliderData() {

            }
        };

        SliderData data;

        // Create
        Slider(Element* parent, SliderData sliderData = SliderData(), StyleList styles = {}, std::string name = "Slider") : Box(parent, styles, name) {

            // Self
            this->data = sliderData;
            this->styles = { &Styles::Self };

                // Label Container
                textContainer = new Element(this, { &Styles::TextContainer });
                
                    // Label and value text
                    labelText = new Text(textContainer, "Value: ", { &Styles::LabelText });
                    valueText = new Text(textContainer, "", { &Styles::ValueText });
                    valueText->setContent(data.val);
                    
                // SliderContainer
                sliderContainer = new Box(this, { &Styles::Slider, &Styles::SliderHover }, "SliderContainer");

                    // Set new value on click
                    sliderContainer->onMouseDown([this] (Event& e) {
                        float newVal = posToVal(e.mouse.pos);
                        if (setVal(newVal)) { refresh(e); }
                    });

                    // Set value on drag
                    sliderContainer->onDrag([this] (Event& e) {
                        float newVal = posToVal(e.mouse.pos);
                        if (setVal(newVal)) { refresh(e); }
                    });

                    // Track
                    track = new Box(sliderContainer, { &Styles::Track }, "Track");
                        thumbContainer = new Element(track, { &Styles::ThumbContainer}, "Container");
                            thumb = new Box(thumbContainer, { &Styles::Thumb, &Styles::ThumbHover }, "Thumb");
        }

        bool setVal(float newVal) {

            // Calc clamped value, check if anything changed
            float clamped = std::clamp(newVal, data.min, data.max);
            if (clamped == data.val) { return false; }
            else { data.val = clamped; valueText->setContent(data.val); return true; }
        }

        float posToVal(Pos& pos) {
            return (data.max - data.min) * track->rect.posWithin(pos).x;
        }

        void computeStyle(Event& e) override {

            float pctVal = (data.val - data.min) / (data.max - data.min);
            track->style->padding.left = Pct(100.0f * pctVal);
        
            Box::computeStyle(e);
        }

        void computePrimitives(Event& e) override {

            Box::computePrimitives(e);
        }
    };
};