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

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace MaterialStatesStyle::Styles {

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

                Style State = {
                    .size = { .width = 100_pct },
                    .margin = { .bottom = 4_px },
                    .padding = { 7_px, 8_px, 7_px, 8_px },
                    .background = { .color = rgba(203, 213, 223, 0.0), .transition = 100_ms },
                    .border = { .radius = 4_px },
                    .text = { .color = rgba(0, 0, 0, 0.85), .size = 14_px },
                    .cursor = Cursor::Hand
                };

                Style StateHover = {
                    .applies = { .hover = true, .focus = true },
                    .background = { .color = rgba(203, 213, 223, 1.0) }
                };

                Style StateSelected = {
                    .background = { .color = rgba(109, 119, 255, 0.22) },
                    .border = { .color = rgba(109, 119, 255, 1.0), .width = 1_px }
                };

                Style StateWorking = {
                    .background = { .color = rgba(255, 255, 255, 0.10) },
                    .border = { .color = rgba(0, 0, 0, 0.25), .width = 1_px },
                    .text = { .color = rgba(0, 0, 0, 0.50), .size = 14_px }
                };

                Style StateChanged = {
                    .border = { .color = rgba(255, 150, 0, 1.0), .width = 1_px }
                };
    };

    using namespace MaterialStatesStyle;

    struct MaterialStates : public Box {

        Cam::App::AppState* app = nullptr;

        Text* title = nullptr;
        Box* list = nullptr;

        std::vector<Text*> states;

        std::function<void(Event&)> onSelectState;

        // Create
        MaterialStates(Element* parent, StyleList styles = {}) : Box(parent, styles, "MaterialStates") {

            app = Cam::App::AppState::Get(shared->state);

            this->styles.add(&Styles::Self);

            title = new Text(this, "Material States", { &Styles::Title });
            list = new Box(this, { &Styles::List }, "MaterialStatesList");
        }

        void selectState(size_t index, Event& e) {

            if (!app) { return; }

            if (index >= app->states.size()) {
                return;
            }

            Cam::App::MaterialState* state = app->states[index];

            if (!state) {
                return;
            }

            app->displayState(state);

            if (onSelectState) {
                onSelectState(e);
            }

            refresh(e);
        }

        std::string stateName(Cam::App::MaterialState* state, size_t index) {

            if (!state) {
                return "Invalid State";
            }

            if (!state->name.empty()) {
                return state->name;
            }

            if (state->working) {

                if (state->model.changed) {
                    return "Working State *";
                }

                return "Working State";
            }

            if (index == 0) {
                return "Final State";
            }

            return "Material State " + std::to_string(index);
        }

        void computeChildren(Event& e) override {

            if (!app) {
                Box::computeChildren(e);
                return;
            }

            size_t oldSize = states.size();
            size_t newSize = app->states.size();

            // Delete old
            for (size_t i = newSize; i < oldSize; i++) {
                delete states[i];
            }

            states.resize(newSize);

            // Add new
            for (size_t i = oldSize; i < newSize; i++) {

                states[i] = new Text(
                    list,
                    "",
                    { &Styles::State, &Styles::StateHover }
                );

                states[i]->onMouseDown([this, i](Event& e) {
                    this->selectState(i, e);
                });
            }

            // Update rows
            for (size_t i = 0; i < newSize; i++) {

                Cam::App::MaterialState* state = app->states[i];

                states[i]->content = stateName(
                    state,
                    i
                );

                bool selected = (
                    state &&
                    app->displayedState == state
                );

                bool working = (
                    state &&
                    state->working
                );

                bool changed = (
                    state &&
                    state->working &&
                    state->model.changed
                );

                if (selected) {
                    states[i]->styles.add(&Styles::StateSelected);
                }

                else {
                    states[i]->styles.remove(&Styles::StateSelected);
                }

                if (working) {
                    states[i]->styles.add(&Styles::StateWorking);
                }

                else {
                    states[i]->styles.remove(&Styles::StateWorking);
                }

                if (changed) {
                    states[i]->styles.add(&Styles::StateChanged);
                }

                else {
                    states[i]->styles.remove(&Styles::StateChanged);
                }
            }

            Box::computeChildren(e);
        }
    };
}