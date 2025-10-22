module;

#include <cmath>
#include <algorithm>

export module Rev.Element.Slider;

import Rev.Core.Pos;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.TextBox;

export namespace Rev::Element {

    namespace Styles {
        
        Style Self = {
            .size = { .width = Grow() },
            .margin = { 4_px, 4_px, 4_px, 4_px },
        };

            Style TextContainer = {
                .margin = { .bottom = 4_px }
            };

                Style LabelText = {
                    .text = { .size = 12_px }
                };

                Style ValueText = {
                    .text = { .size = 12_px }
                };

            Style Slider = {
                .size = { .width = Grow() },
                .padding = { 6_px, 6_px, 6_px, 6_px },
                .alignment = { Axis::Vertical, Align::Start, Align::Center },
                .border = { .radius = 4_px },
                .background = { .color = rgba(255, 255, 255, 0.1), .transition = 0.1_sec },
            };

                Style SliderHover = {
                    .background = { .color = rgba(255, 255, 255, 0.15) }
                };

            Style Track = {
                .size = { .width = 100_pct, .height = 2_px, .minWidth = 100_px },
                .alignment = { Axis::Vertical, Align::Start, Align::Center, Break::True },
                .background = { .color = rgba(255, 255, 255, 0.25) },
            };

                Style ThumbContainer = {
                    .size = { .width = 0_px, .height = 0_px },
                    .alignment = { Axis::Vertical, Align::Center, Align::Center }
                };

                    Style Thumb = {
                        .size = { .width = 4_px, .height = 8_px, .transition = 100 },
                        .background = { .color = rgba(255, 255, 255, 0.5) },
                    };

                    Style ThumbHover = {
                        .size = { .width = 8_px, .height = 16_px }
                    };
    };

    struct Slider : public Box {

        // Text
        Element* textContainer = nullptr;
            TextBox* labelText = nullptr;
            TextBox* valueText = nullptr;

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
        Slider(Element* parent, SliderData sliderData = SliderData()) : Box(parent, "Slider") {

            // Self
            this->data = sliderData;
            this->styles = { &Styles::Self };

                // Label Container
                textContainer = new Element(this);
                textContainer->styles = { &Styles::TextContainer };
                
                    // Label text
                    labelText = new TextBox(textContainer, "Value: ");
                    labelText->styles = { &Styles::LabelText };

                    // Value textContainer
                    valueText = new TextBox(textContainer);
                    valueText->styles = { &Styles::ValueText };
                    valueText->setContent(data.val);
                    
                // SliderContainer
                sliderContainer = new Box(this, "SliderContainer");
                sliderContainer->styles = { &Styles::Slider };
                sliderContainer->hoverStyle = { &Styles::SliderHover };

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
                    track = new Box(sliderContainer, "Track");
                    track->styles = { &Styles::Track };

                        // Thumb container
                        thumbContainer = new Element(track, "Container");
                        thumbContainer->styles = { &Styles::ThumbContainer };

                            thumb = new Box(thumbContainer, "Thumb");
                            thumb->styles = { &Styles::Thumb };
                            thumb->hoverStyle = Styles::ThumbHover;
                            thumb->dragStyle = Styles::ThumbHover;
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
    };
};