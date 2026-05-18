module;

#include <string>
#include <vector>
#include <functional>

export module Cam.Gui.MaterialStates;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;

import Cam.App;
import Cam.App.MaterialState;
import Cam.Gui.MaterialState;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace MaterialStatesStyle {

        Shadow subtleShadow = {
            .color = rgba(0, 0, 0, 0.35),
            .size = Px(-8),
            .blur = 16_px
        };

        Style Self = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 220_px, .height = Grow() },
            .margin = { 4_px, 4_px, 4_px, 4_px },
            .padding = { 8_px, 8_px, 8_px, 8_px },
            .background = { .color = rgba(0, 0, 0, 0.1) },
            .border = { .radius = 6_px },
            .shadow = subtleShadow,
            .zIndex = +1
        };

        Style Title = {
            .size = { 100_pct },
            .margin = { .bottom = 8_px },
            .text = { .color = rgba(0, 0, 0, 0.65), .size = 13_px }
        };

        Style List = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { 100_pct },
            .overflow = Overflow::Hide
        };
    };

    using namespace MaterialStatesStyle;

    struct MaterialStates : public Box {

        Cam::App::AppState* app = nullptr;

        Text* title = nullptr;
        Box* list = nullptr;

        std::vector<MaterialState*> rows;

        std::function<void(Event&)> onSelectState;

        // Create
        MaterialStates(Element* parent, StyleList styles = {}) : Box(parent, styles, "MaterialStates") {

            app = Cam::App::AppState::Get(shared->state);

            this->styles.add(&MaterialStatesStyle::Self);

            title = new Text(this, "Material States", { &MaterialStatesStyle::Title });
            list = new Box(this, { &MaterialStatesStyle::List }, "MaterialStatesList");
        }

        void selectState(Cam::App::MaterialState* state, Event& e) {

            if (!app || !state) { return; }

            app->displayState(state);

            if (onSelectState) { onSelectState(e); }

            refresh(e);
        }

        void computeChildren(Event& e) override {

            if (!app) {
                Box::computeChildren(e);
                return;
            }

            size_t oldSize = rows.size();
            size_t newSize = app->states.size();

            for (size_t i = newSize; i < oldSize; i++) {
                delete rows[i];
            }

            rows.resize(newSize);

            for (size_t i = oldSize; i < newSize; i++) {

                rows[i] = new MaterialState(list);

                rows[i]->onSelect = [this](Event& e, Cam::App::MaterialState* state) {
                    this->selectState(state, e);
                };
            }

            for (size_t i = 0; i < newSize; i++) {
                rows[i]->setState(app->states[i], i);
            }

            Box::computeChildren(e);
        }
    };
}