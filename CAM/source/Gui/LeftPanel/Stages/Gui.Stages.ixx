module;

#include <string>
#include <vector>
#include <functional>
#include <algorithm>

export module Cam.Gui.Stages;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;

import Cam.App;
import Cam.App.Project;
import Cam.App.Stage;
import Cam.Gui.StageRow;
import Cam.Gui.ToolPathSettingsWindow;
import Cam.Gui.Theme;

import Rev.Window;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace StagesStyle {

        Style Self = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 100_pct },
            .margin = { .top = 2_px, .bottom = 2_px },
            .padding = { .left = 2_px, .right = 2_px, .top = 4_px, .bottom = 4_px },
            .zIndex = +1
        };

        Style Title = {
            .size = { 100_pct },
            .margin = { .bottom = 4_px },
            .text = { .size = 12_px }
        };

        Style List = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            //.overflow = Overflow::Show,
            .size = { .width = 100_pct },
            //.background = { .color = rgba(255, 0, 0, 1.0f) }
        };
    };

    using namespace StagesStyle;

    struct Stages : public Box {

        Cam::App::AppState* app = nullptr;

        Text* title = nullptr;
        Box* list = nullptr;

        std::vector<StageRow*> rows;

        // Dev stress test: cycles DOM child order without destroying rows.
        size_t domPermuteStep = 0;

        ToolPathSettingsWindow* settingsWindow = nullptr;

        std::function<void(Event&)> onSelectState;
        std::function<void(Event&)> onDeleteState;
        std::function<void(Event&)> onToolPathEdited;

        Stages(Element* parent, StyleList styles = {}) : Box(parent, styles, "Stages") {

            app = Cam::App::AppState::Get(shared->state);

            this->styles.add(&StagesStyle::Self);

            title = new Text(
                this,
                "Material States",
                Theme::withMutedText({ &StagesStyle::Title })
            );

            list = new Box(
                this,
                { &StagesStyle::List },
                "StagesList"
            );
        }

        ~Stages() {
            closeSettingsWindow();
        }

        Cam::App::Project* activeProject() {

            if (!app) { return nullptr; }

            return app->activeProject;
        }

        void selectState(Cam::App::Stage* state, Event& e) {

            if (!app || !state) { return; }

            if (app->selectStage(state, e.keyboard.shift)) {

                if (onSelectState) { onSelectState(e); }

                refresh(e);
            }
        }

        void deleteState(Cam::App::Stage* state, Event& e) {

            if (!app || !state) { return; }

            closeSettingsWindowForState(state);
            clearRowsForState(state);

            if (app->deleteStage(state)) {

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

        void closeSettingsWindowForState(Cam::App::Stage* state) {

            if (!settingsWindow || !state) { return; }

            if (settingsWindow->state && state->contains(settingsWindow->state)) {
                closeSettingsWindow();
            }
        }

        // Reorder row elements in the list box to exercise shared-style + DOM churn.
        void stressTestPermuteRows(Event& e) {

            if (!list || rows.size() < 2) {
                return;
            }

            domPermuteStep++;

            // Pointer order is stable for next_permutation; wrap by sorting when exhausted.
            auto rowLess = [](StageRow* a, StageRow* b) {
                return static_cast<const void*>(a) < static_cast<const void*>(b);
            };

            if (!std::next_permutation(rows.begin(), rows.end(), rowLess)) {
                std::sort(rows.begin(), rows.end(), rowLess);
            }

            for (StageRow* row : rows) {
                list->removeChild(row);
            }

            for (StageRow* row : rows) {
                list->addChild(row);
            }

            refresh(e);
        }

        void clearRowsForState(Cam::App::Stage* state) {

            if (!state) { return; }

            for (StageRow* row : rows) {

                if (!row || !row->state) { continue; }

                if (state->contains(row->state)) {
                    row->setState(nullptr, row->index);
                }
            }
        }

        static std::string settingsTitleFor(Cam::App::Stage* state, size_t index) {

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

        void changeToolPathTool(
            Cam::App::Stage* state,
            const std::string& toolName,
            Event& e
        ) {

            if (!app || !state || toolName.empty()) { return; }

            Cam::App::Project* project = activeProject();

            if (!project) { return; }

            if (!app->saveToolPathSettings(
                state,
                state->toolPath.strategy,
                toolName,
                state->toolPath.stepDown,
                state->toolPath.stepover,
                state->toolPath.feedRate,
                state->toolPath.rapidSpeedMmPerSec,
                state->toolPath.climbMilling,
                state->toolPath.linkRetractDistance
            )) {
                return;
            }

            if (onToolPathEdited) {
                onToolPathEdited(e);
            }

            refresh(e);
        }

        void openToolPathSettings(Cam::App::Stage* state, Event& e) {

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
            size_t newSize = project->stages.size();

            for (size_t i = newSize; i < oldSize; i++) {
                delete rows[i];
            }

            rows.resize(newSize);

            for (size_t i = oldSize; i < newSize; i++) {

                rows[i] = new StageRow(list);

                rows[i]->onSelect = [this](Event& ev, Cam::App::Stage* state) {
                    this->selectState(state, ev);
                };

                rows[i]->onDelete = [this](Event& ev, Cam::App::Stage* state) {
                    this->deleteState(state, ev);
                };

                rows[i]->onOpenToolPathSettings = [this](Event& ev, Cam::App::Stage* state) {
                    this->openToolPathSettings(state, ev);
                };

                // Toggling a component's visibility re-syncs the 3D view via the
                // same path tool-path edits use, then refreshes the row glyphs.
                rows[i]->onComponentToggled = [this](Event& ev) {
                    if (onToolPathEdited) { onToolPathEdited(ev); }
                    refresh(ev);
                };
            }

            for (size_t i = 0; i < newSize; i++) {

                rows[i]->setState(project->stages[i], i);

                rows[i]->onToolPathToolChanged = [this](
                    Event& ev,
                    Cam::App::Stage* state,
                    const std::string& toolName
                ) {
                    this->changeToolPathTool(state, toolName, ev);
                };
            }

            Box::computeChildren(e);
        }
    };
}
