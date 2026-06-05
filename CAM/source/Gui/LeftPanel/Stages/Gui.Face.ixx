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

        // A face is a small, semi-rounded chip with a hover background. It is
        // deliberately unsized — it hugs its label — and relies on its parent
        // (the operation body) to lay faces out horizontally.
        Style Self = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .margin = { .right = 4_px, .top = 1_px, .bottom = 1_px },
            .padding = { .left = 6_px, .right = 6_px, .top = 2_px, .bottom = 2_px },
            .border = { .radius = 4_px },
            .cursor = Cursor::Hand
        };

        Style Hover = {
            .applies = { .hover = true },
            .background = { .color = rgba(255, 255, 255, 0.10), .transition = 100_ms }
        };

        Style Selected = {
            .background = { .color = rgba(255, 255, 255, 0.16) }
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

        void setSelected(bool selected) {
            if (selected) { styles.add(&FaceStyle::Selected); }
            else          { styles.remove(&FaceStyle::Selected); }
        }
    };
}
