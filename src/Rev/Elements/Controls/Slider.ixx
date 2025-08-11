module;

#include <algorithm>

export module Rev.Slider;

import Rev.Style;
import Rev.Element;
import Rev.Box;
import Rev.TextBox;

export namespace Rev {

    namespace Styles {

        Style Slider = {

            .size = { .width = Grow(), .minWidth = 100_px },
            .margin = { 4_px, 4_px, 4_px, 4_px },
            .padding = { 6_px, 6_px, 6_px, 6_px },
            .alignment = { Axis::Vertical, Align::Start, Align::Center },
            .border = { .radius = 4_px },
            .background = { .color = Color(1, 1, 1, 0.1f), .transition = 200 },
        };

        Style SliderHover = {
            .background = { .color = Color(1, 1, 1, 0.2f) }
        };

        Style Track = {
            .size = { .width = 100_pct, .height = 2_px },
            .alignment = { Axis::Vertical, Align::Start, Align::Center, Break::True },
            .background = { .color = Color(1, 1, 1, 0.25f) },
            .transition = 1000
        };

        Style ThumbContainer = {
            .size = { .width = 0_px, .height = 0_px },
            .alignment = { Axis::Vertical, Align::Center, Align::Center }
        };

        Style Thumb = {
            .size = { .width = 4_px, .height = 8_px, .transition = 200 },
            .background = { .color = Color(1, 1, 1, 0.5f) , .transition = 200 },
        };
    };

    struct Slider : public Box {

        TextBox* label = nullptr;
        Box* track = nullptr;
        Element* thumbContainer = nullptr;
        Box* thumb = nullptr;

        struct SliderData {

            float min = 0;
            float max = 1;
            float def = 0.5;
            float val = 1.0;
        };

        SliderData data;

        // Create
        Slider(Element* parent, SliderData sliderData = SliderData()) : Box(parent, "Slider") {

            this->includeChildren = true;

            this->data = sliderData;

            this->style = Styles::Slider;
            this->hoverStyle = Styles::SliderHover;
            this->dragStyle = Styles::SliderHover;

            label = new TextBox(this);
            label->style = {
                .margin = { .bottom = 4_px },
                .alignment = { .breakWrap = Break::True }
            };

            track = new Box(this, "Track");
            track->includeChildren = true;

            thumbContainer = new Element(track, "Container");
            thumbContainer->includeChildren = true;

            thumb = new Box(thumbContainer, "Thumb");

            thumb->onMouseMove([this](Event& e) {
                bool test = true;
            });

            thumb->hoverStyle = { 
                .size = { .width = 100_px, .height = 100_px },
            };

            track->styles = { &Styles::Track };
            thumbContainer->styles = { &Styles::ThumbContainer };
            thumb->styles = { &Styles::Thumb };
        }

        // Destroy
        ~Slider() {

        }

        bool setVal(float newVal) {

            // Calc clamped value, check if anything changed
            float clamped = std::clamp(newVal, data.min, data.max);
            if (clamped == data.val) { return false; }
            else { data.val = clamped; return true; }
        }

        float posToVal(Pos& pos) {
            return (data.max - data.min) * track->rect.posWithin(pos).x;
        }

        void mouseDown(Event& e) override {

            float newVal = posToVal(e.mouse.pos);
            if (setVal(newVal)) { refresh(e); }

            Element::mouseDown(e);
        }

        void mouseDrag(Event& e) override {

            float newVal = posToVal(e.mouse.pos);
            if (setVal(newVal)) { refresh(e); }

            Element::mouseDrag(e);
        }

        void mouseEnter(Event& e) override {
            refresh(e);
            Box::mouseEnter(e);
        }

        void mouseLeave(Event& e) override {
            refresh(e);
            Box::mouseLeave(e);
        }

        void computeStyle(Event& e) override {

            float pctVal = (data.val - data.min) / (data.max - data.min);
            
            // Position the thumb via track padding
            track->style = Styles::Track; // make a copy before mutating
            track->style->padding.left = Pct(100.0f * pctVal);
        
            // Style the thumb background color based on slider value
            thumb->style = Styles::Thumb; // copy base style
        
            if (pctVal > 0.5f) {
                thumb->style->background.color = Color(1.0f, 0.2f, 0.2f, 0.9f); // reddish
            }
        
            Element::computeStyle(e);
        }
        
    };
};