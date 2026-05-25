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
import Cam.App.Project;
import Cam.App.MaterialState;
import Cam.Gui.MaterialState;
import Cam.Gui.ToolPathSettingsWindow;

import Rev.Window;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace MaterialStatesStyle {

        Style Self = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .height = Grow() },
            .margin = { 2_px, 2_px, 2_px, 2_px },
            .padding = { 4_px, 6_px, 4_px, 6_px },
            .zIndex = +1
        };

        Style Title = {
            .size = { 100_pct },
            .margin = { .bottom = 4_px },
            .text = { .color = rgba(100, 116, 139, 1.0), .size = 12_px }
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

        ToolPathSettingsWindow* settingsWindow = nullptr;

        std::function<void(Event&)> onSelectState;
        std::function<void(Event&)> onDeleteState;
        std::function<void(Event&)> onToolPathEdited;

        MaterialStates(Element* parent, StyleList styles = {}) : Box(parent, styles, "MaterialStates") {

            app = Cam::App::AppState::Get(shared->state);

            this->styles.add(&MaterialStatesStyle::Self);

            title = new Text(
                this,
                "Material States",
                { &MaterialStatesStyle::Title }
            );

            list = new Box(
                this,
                { &MaterialStatesStyle::List },
                "MaterialStatesList"
            );
        }

        ~MaterialStates() {
            closeSettingsWindow();
        }

        Cam::App::Project* activeProject() {

            if (!app) { return nullptr; }

            return app->activeProject;
        }

        void selectState(Cam::App::MaterialState* state, Event& e) {

            if (!app || !state) { return; }

            if (app->selectState(state, e.keyboard.shift)) {

                if (onSelectState) { onSelectState(e); }

                refresh(e);
            }
        }

        void deleteState(Cam::App::MaterialState* state, Event& e) {

            if (!app || !state) { return; }

            closeSettingsWindowForState(state);
            clearRowsForState(state);

            if (app->deleteState(state)) {

                if (onDeleteState) { onDeleteState(e); }

                refresh(e);
            }
        }

        void closeSettingsWindow() {

            if (!settingsWindow) {
                return;
            }

            settingsWindow->shouldClose = true;
            settingsWindow->state = nullptr;
            settingsWindow->onSaved = nullptr;
            settingsWindow->onClosed = nullptr;
            settingsWindow = nullptr;
        }

        void closeSettingsWindowForState(Cam::App::MaterialState* state) {

            if (!settingsWindow || !state) { return; }

            if (settingsWindow->state && state->contains(settingsWindow->state)) {
                closeSettingsWindow();
            }
        }

        void clearRowsForState(Cam::App::MaterialState* state) {

            if (!state) { return; }

            for (MaterialState* row : rows) {

                if (!row || !row->state) { continue; }

                if (state->contains(row->state)) {
                    row->setState(nullptr, row->index);
                }
            }
        }

        static std::string settingsTitleFor(Cam::App::MaterialState* state, size_t index) {

            if (!state) { return "Material State"; }

            if (!state->name.empty()) {
                return state->name;
            }

            if (state->working) {
                return "Working State";
            }

            if (index == 0) { return "Final State"; }

            return "Material State " + std::to_string(index);
        }

        void openToolPathSettings(Cam::App::MaterialState* state, Event& e) {

            if (!state || !state->parent) { return; }

            Cam::App::Project* project = activeProject();
            if (!project) { return; }

            size_t index = project->indexOf(state);

            Rev::Window* owner = ToolPathSettingsWindow::rootWindow(this);
            if (!owner || !owner->shared) { return; }

            closeSettingsWindow();

            settingsWindow = new ToolPathSettingsWindow(
                owner,
                state,
                settingsTitleFor(state, index)
            );

            settingsWindow->onSaved = [this](Event& savedEvent) {

                if (onToolPathEdited) {
                    onToolPathEdited(savedEvent);
                }

                refresh(savedEvent);
            };

            settingsWindow->onClosed = [this](Event& closedEvent) {

                if (onToolPathEdited) {
                    onToolPathEdited(closedEvent);
                }

                refresh(closedEvent);
            };
        }

        void computeChildren(Event& e) override {

            if (settingsWindow && settingsWindow->shouldClose) {
                settingsWindow = nullptr;
            }

            Cam::App::Project* project = activeProject();

            if (!project) {
                Box::computeChildren(e);
                return;
            }

            size_t oldSize = rows.size();
            size_t newSize = project->states.size();

            for (size_t i = newSize; i < oldSize; i++) {
                delete rows[i];
            }

            rows.resize(newSize);

            for (size_t i = oldSize; i < newSize; i++) {

                rows[i] = new MaterialState(list);

                rows[i]->onSelect = [this](Event& ev, Cam::App::MaterialState* state) {
                    this->selectState(state, ev);
                };

                rows[i]->onDelete = [this](Event& ev, Cam::App::MaterialState* state) {
                    this->deleteState(state, ev);
                };

                rows[i]->onOpenToolPathSettings = [this](Event& ev, Cam::App::MaterialState* state) {
                    this->openToolPathSettings(state, ev);
                };
            }

            for (size_t i = 0; i < newSize; i++) {
                rows[i]->setState(project->states[i], i);
            }

            Box::computeChildren(e);
        }
    };
}
