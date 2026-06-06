module;

#include <string>
#include <functional>

export module Cam.Gui.Face;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;

import Cam.App.Model;
import Cam.Gui.Theme;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace FaceStyle {

        // A face is a small, semi-rounded chip. It is deliberately unsized — it
        // hugs its label. Its background is set per-frame from Theme::distinct so
        // it stays a touch lighter/darker than its surroundings in either theme
        // (see Face::computeStyle); only the transition lives in the style here.
        Style Self = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .margin = { .right = 4_px, .top = 1_px, .bottom = 1_px },
            .padding = { .left = 6_px, .right = 6_px, .top = 2_px, .bottom = 2_px },
            .border = { .radius = 4_px },
            .background = { .transition = 100_ms },
            .cursor = Cursor::Hand
        };

        // Carries no properties — its only job is to flag the chip as having a
        // hover style so the framework re-styles it on hover-change; the actual
        // hover colour is computed in Face::computeStyle via Theme::distinct.
        Style Hover = {
            .applies = { .hover = true }
        };

        Style Label = {
            .overflow = Overflow::Hide,
            .text = { .size = 11_px, .wrap = Wrap::False }
        };
    }

    // A GUI element representing a single face of a model. It carries the face
    // it represents as a by-value Model::Face handle (model + id), so it never
    // dangles across geometry rebuilds — the host rebuilds these elements from
    // current model state. Supports hover (→ highlight) and click (→ select).
    struct Face : public Box {

        Cam::App::Model::Face face;

        Text* label = nullptr;

        bool selected = false;

        // Resting / hover / selected overlay strengths (over the surroundings).
        static constexpr float RestAlpha     = 0.05f;
        static constexpr float HoverAlpha    = 0.11f;
        static constexpr float SelectedAlpha = 0.15f;

        // Last applied overlay + mode, so we only re-style when they change.
        float appliedAlpha = -1.0f;
        Theme::Mode appliedMode = Theme::Mode::Light;

        std::function<void(Event&, Cam::App::Model::Face)> onHover;
        std::function<void(Event&)> onUnhover;
        std::function<void(Event&, Cam::App::Model::Face)> onSelect;

        Face(Element* parent, StyleList styles = {}) : Box(parent, styles, "Face") {

            this->styles.add(&FaceStyle::Self);
            this->styles.add(&FaceStyle::Hover);

            label = new Text(
                this, "",
                Theme::layer({ &FaceStyle::Label }, { &Theme::Styles::Text })
            );

            this->onMouseEnter([this](Event& e) {
                if (onHover) { onHover(e, face); }
            });

            this->onMouseLeave([this](Event& e) {
                if (onUnhover) { onUnhover(e); }
            });

            this->onClick([this](Event& e) {
                e.propagate = false;
                if (onSelect) { onSelect(e, face); }
            });
        }

        void setFace(Cam::App::Model::Face f, const std::string& text) {
            face = f;
            if (label) { label->content = text; }
        }

        void setSelected(bool value) {
            selected = value;
        }

        // Per-frame style: keep the chip a touch distinct from its surroundings
        // in either theme, brighter on hover, brighter still when selected.
        void computeStyle(Event& e) override {

            float alpha = targetFlags.hover ? HoverAlpha : RestAlpha;
            if (selected) { alpha = SelectedAlpha; }

            const Theme::Mode currentMode = Theme::currentMode();

            if (alpha != appliedAlpha || currentMode != appliedMode) {
                appliedAlpha = alpha;
                appliedMode = currentMode;
                style->background.color = Theme::distinct(alpha);
                this->dirty.style = true;
            }

            Box::computeStyle(e);
        }
    };
}
