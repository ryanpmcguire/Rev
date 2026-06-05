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
import Cam.Gui.Theme;

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
            .size = { .width = 100_pct }
        };
    };

    using namespace StagesStyle;

    // The stage list. Each row (StageRow) owns its own settings window, so this
    // manager no longer tracks a single shared window — multiple stages can have
    // their settings windows open at once.
    struct Stages : public Box {

        Cam::App::AppState* app = nullptr;

        Text* title = nullptr;
        Box* list = nullptr;

        std::vector<StageRow*> rows;

        // Dev stress test: cycles DOM child order without destroying rows.
        size_t domPermuteStep = 0;

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

        void computeChildren(Event& e) override {

            Cam::App::Project* project = activeProject();

            if (!project) {
                Box::computeChildren(e);
                return;
            }

            size_t oldSize = rows.size();
            size_t newSize = project->stages.size();

            // Deleting a row also retires (closes) any settings window it owned.
            for (size_t i = newSize; i < oldSize; i++) {
                delete rows[i];
            }

            rows.resize(newSize);

            for (size_t i = oldSize; i < newSize; i++) {

                rows[i] = new StageRow(list);

                rows[i]->onSelect = [this](Event& ev, Cam::App::Stage* state) {
                    this->selectState(state, ev);
                };

                // Saving in (or closing) a stage's settings window re-syncs the
                // 3D view via the same path tool-path edits use.
                rows[i]->onSettingsChanged = [this](Event& ev) {
                    if (onToolPathEdited) { onToolPathEdited(ev); }
                    refresh(ev);
                };

                // Toggling a component's visibility re-syncs the 3D view too.
                rows[i]->onComponentToggled = [this](Event& ev) {
                    if (onToolPathEdited) { onToolPathEdited(ev); }
                    refresh(ev);
                };
            }

            for (size_t i = 0; i < newSize; i++) {
                rows[i]->setState(project->stages[i], i);
            }

            Box::computeChildren(e);
        }
    };
}
