module;

#include <string>
#include <functional>

#include <managed.hpp>

export module Cam.Gui.MaterialState;

import Rev.Core.Resource;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Svg;

import Cam.App;
import Cam.App.MaterialState;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace MaterialStateStyle::Styles {

        Style Self = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .width = 100_pct },
            .margin = { .bottom = 4_px },
            .padding = { 7_px, 8_px, 7_px, 8_px },
            .background = { .color = rgba(203, 213, 223, 0.0), .transition = 100_ms },
            .border = { .radius = 4_px },
            .cursor = Cursor::Hand
        };

        Style Hover = {
            .applies = { .hover = true, .focus = true },
            .background = { .color = rgba(203, 213, 223, 1.0) }
        };

        Style Selected = {
            .background = { .color = rgba(109, 119, 255, 0.22) },
            .border = { .color = rgba(109, 119, 255, 1.0), .width = 1_px }
        };

        Style Working = {
            .background = { .color = rgba(255, 255, 255, 0.10) },
            .border = { .color = rgba(0, 0, 0, 0.25), .width = 1_px }
        };

        Style Changed = {
            .border = { .color = rgba(255, 150, 0, 1.0), .width = 1_px }
        };

        Style Label = {
            .size = { .width = Grow() },
            .text = { .color = rgba(0, 0, 0, 0.85), .size = 14_px }
        };

        Style LabelWorking = {
            .text = { .color = rgba(0, 0, 0, 0.50), .size = 14_px }
        };

        Style DeleteIcon = {
            .size = { 24_px, 24_px },
            .margin = { .left = 6_px },
            .text = { .color = rgba(0.72, 0.72, 0.72, 1.0), .transition = 100_ms },
            .cursor = Cursor::Hand
        };

        Style DeleteIconHover = {
            .applies = { .hover = true, .focus = true },
            .text = { .color = rgba(0.45, 0.45, 0.45, 1.0) }
        };

        Style DeleteIconPress = {
            .applies = { .press = true },
            .text = { .color = rgba(0.05, 0.05, 0.05, 1.0) }
        };

        Style DeleteIconDisabled = {
            .applies = { .disabled = true },
            .text = { .color = rgba(0.78, 0.78, 0.78, 0.35) },
            .cursor = Cursor::Default
        };
    };

    using namespace MaterialStateStyle;

    struct MaterialState : public Box {

        Cam::App::AppState* app = nullptr;
        Cam::App::MaterialState* state = nullptr;

        size_t index = 0;

        Text* label = nullptr;
        Svg* deleteIcon = nullptr;

        std::function<void(Event&, Cam::App::MaterialState*)> onSelect;
        std::function<void(Event&, Cam::App::MaterialState*)> onDelete;

        // Create
        MaterialState(Element* parent, StyleList styles = {}) : Box(parent, styles, "MaterialState") {

            app = Cam::App::AppState::Get(shared->state);

            this->styles.add(&Styles::Self);
            this->styles.add(&Styles::Hover);

            label = new Text(this, "", { &Styles::Label });

            deleteIcon = new Svg(
                this,
                File("./delete.svg"),
                {
                    &Styles::DeleteIcon,
                    &Styles::DeleteIconHover,
                    &Styles::DeleteIconPress,
                    &Styles::DeleteIconDisabled
                },
                "DeleteMaterialState"
            );

            deleteIcon->onMouseDown([this](Event& e) {
                if (!canDelete()) { return; }
                if (onDelete && state) { onDelete(e, state); }
                e.propagate = false;
            });

            this->onMouseDown([this](Event& e) {
                if (onSelect && state) { onSelect(e, state); }
            });
        }

        void setState(Cam::App::MaterialState* state, size_t index) {
            this->state = state;
            this->index = index;
        }

        std::string stateName() {

            if (!state) { return "Invalid State"; }

            if (!state->name.empty()) {
                if (state->working && state->model.changed) { return state->name + " *"; }
                return state->name;
            }

            if (state->working) {
                if (state->model.changed) { return "Working State *"; }
                return "Working State";
            }

            if (index == 0) { return "Final State"; }

            return "Material State " + std::to_string(index);
        }

        bool canDelete() {
            if (!state) { return false; }
            if (!state->parent) { return false; }
            return true;
        }

        void computeChildren(Event& e) override {

            bool selected = (app && state && app->displayedState == state);
            bool working = (state && state->working);
            bool changed = (state && state->working && state->model.changed);

            if (label) {
                label->content = stateName();

                if (working) { label->styles.add(&Styles::LabelWorking); }
                else { label->styles.remove(&Styles::LabelWorking); }
            }

            if (deleteIcon) {
                deleteIcon->resolved.disabled = !canDelete();
            }

            if (selected) { styles.add(&Styles::Selected); }
            else { styles.remove(&Styles::Selected); }

            if (working) { styles.add(&Styles::Working); }
            else { styles.remove(&Styles::Working); }

            if (changed) { styles.add(&Styles::Changed); }
            else { styles.remove(&Styles::Changed); }

            Box::computeChildren(e);
        }
    };
}