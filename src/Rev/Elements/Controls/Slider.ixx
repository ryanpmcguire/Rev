module;

#include <algorithm>

export module Rev.Slider;

import Rev.Element;
import Rev.Box;

export namespace Rev {

    struct Slider : public Box {

        static inline Style sliderStyle = {

            .size = { .width = Grow(), .minWidth = 100_px },
            .margin = { 4_px, 4_px, 4_px, 4_px },
            .padding = { 6_px, 6_px, 6_px, 6_px },
            .alignment = { Axis::Vertical, Align::Center, Align::Center },
            .border = { .radius = 4_px },
            .background = { .color = Color(1, 1, 1, 0.1f) }
        };

        static inline Style trackStyle = {
            .size = { .width = 100_pct, .height = 2_px },
            .alignment = { Axis::Vertical, Align::Start, Align::Center },
            .background = { .color = Color(1, 1, 1, 0.25f) }
        };

        static inline Style thumbContainerStyle = {
            .size = { .width = 0_px, .height = 0_px },
            .alignment = { Axis::Vertical, Align::Center, Align::Center }
        };

        static inline Style thumbStyle = {
            .size = { .width = 4_px, .height = 8_px },
            .background = { .color = Color(1, 1, 1, 0.5f) }
        };

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

            this->data = sliderData;

            this->style = sliderStyle;

            track = new Box(this, "Track");
            thumbContainer = new Element(track, "Container");
            thumb = new Box(thumbContainer, "Thumb");

            track->style = trackStyle;
            thumbContainer->style = thumbContainerStyle;
            thumb->style = thumbStyle;
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

        void computeStyle(Event& e) {

            float pctVal = (data.val - data.min) / (data.max - data.min);
            
            track->style.padding.left = Pct(100.0f * pctVal);

            Element::computeStyle(e);
        }
    };
};