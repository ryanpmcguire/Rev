module;

#include <string>
#include <vector>

export module Cam.App;

import Rev.OS.File;

import Cam.App.Model;
import Cam.App.MaterialState;

export namespace Cam::App {

    struct AppState {

        Rev::OS::File file = Rev::OS::File({
            .pathname = "C:/Users/Ryan/Desktop/Ryan/recils/parts/Nut Mount (Cross Mounted) (Chamfered).STEP"
        });

        std::vector<MaterialState*> states;

        MaterialState* rootState = nullptr;
        MaterialState* latestCommittedState = nullptr;
        MaterialState* workingState = nullptr;
        MaterialState* displayedState = nullptr;

        AppState() {
            loadDefaultModel();
        }

        ~AppState() {

            for (MaterialState* state : states) {
                delete state;
            }

            states.clear();
        }

        void loadDefaultModel() {

            for (MaterialState* state : states) {
                delete state;
            }

            states.clear();

            rootState = MaterialState::FromStep(file);

            states.push_back(rootState);

            latestCommittedState = rootState;

            workingState = MaterialState::FromPriorState(latestCommittedState);
            states.push_back(workingState);

            displayedState = workingState;
        }

        Model* getDisplayedModel() {

            if (!displayedState) {
                return nullptr;
            }

            return &displayedState->model;
        }

        void displayState(MaterialState* state) {

            if (!state) {
                return;
            }

            displayedState = state;
        }

        bool defeatureSelected() {

            if (!workingState) { return false; }

            displayedState = workingState;

            bool ok = workingState->model.defeatureSelected();

            if (!ok) { return false; }

            workingState->model.clearSelection();
            workingState->computeDelta();

            return true;
        }

        bool commitWorkingState() {

            if (!workingState) {
                return false;
            }

            if (!workingState->model.changed) {
                return false;
            }

            workingState->committed = true;
            workingState->working = false;
            workingState->model.clearSelection();
            workingState->model.changed = false;

            workingState->computeDelta();

            workingState->name = "Material State " + std::to_string(committedCount());

            latestCommittedState = workingState;

            workingState = MaterialState::FromPriorState(latestCommittedState);
            states.push_back(workingState);

            displayedState = workingState;

            return true;
        }

        bool removeState(MaterialState* state) {

            if (!state) { return false; }
            if (state == rootState) { return false; }

            MaterialState* fallback = state->parent;

            std::vector<MaterialState*> removed;
            state->collectSubtree(removed);

            auto willRemove = [&removed](MaterialState* p) {

                return std::find(
                    removed.begin(),
                    removed.end(),
                    p
                ) != removed.end();
            };

            states.erase(
                std::remove_if(
                    states.begin(),
                    states.end(),
                    [&](MaterialState* p) {
                        return willRemove(p);
                    }
                ),
                states.end()
            );

            if (willRemove(latestCommittedState)) {
                latestCommittedState = fallback ? fallback : rootState;
            }

            if (willRemove(displayedState)) {
                displayedState = nullptr;
            }

            if (willRemove(workingState)) {
                workingState = nullptr;
            }

            state->remove();

            if (!latestCommittedState) {
                latestCommittedState = rootState;
            }

            if (!workingState) {
                workingState = MaterialState::FromPriorState(latestCommittedState);
                states.push_back(workingState);
            }

            if (!displayedState) {
                displayedState = workingState;
            }

            return true;
        }

        size_t committedCount() const {

            size_t count = 0;

            for (MaterialState* state : states) {
                if (state && state->committed) {
                    count += 1;
                }
            }

            return count;
        }

        static AppState* Get(void* pState) {
            return static_cast<AppState*>(pState);
        }
    };
}