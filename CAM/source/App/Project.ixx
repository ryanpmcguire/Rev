module;

#include <vector>
#include <algorithm>

export module Cam.App.Project;

import Rev.OS.File;

import Cam.App.Model;
import Cam.App.MaterialState;

export namespace Cam::App {

    struct Project {

        Rev::OS::File file = Rev::OS::File({
            .pathname = "C:/Users/Ryan/Desktop/Ryan/recils/parts/Nut Mount (Cross Mounted) (Chamfered).STEP"
        });

        std::vector<MaterialState*> states;

        MaterialState* rootState = nullptr;
        MaterialState* latestCommittedState = nullptr;
        MaterialState* workingState = nullptr;
        MaterialState* displayedState = nullptr;

        Project() {
            loadDefaultModel();
        }

        ~Project() {
            clear();
        }

        void clear() {
            for (MaterialState* state : states) {
                delete state;
            }

            states.clear();

            rootState = nullptr;
            latestCommittedState = nullptr;
            workingState = nullptr;
            displayedState = nullptr;
        }

        void loadDefaultModel() {
            clear();

            rootState = MaterialState::FromStep(file);

            states.push_back(rootState);

            latestCommittedState = rootState;
            workingState = MaterialState::FromPriorState(rootState);

            states.push_back(workingState);

            displayedState = workingState;
        }

        Cam::App::Model* getDisplayedModel() {
            if (!displayedState) { return nullptr; }
            return &displayedState->model;
        }

        bool selectState(MaterialState* state) {
            if (!state) { return false; }

            displayedState = state;
            return true;
        }

        bool deleteState(MaterialState* state) {
            if (!state) { return false; }
            if (state == rootState) { return false; }

            MaterialState* fallback = state->parent;

            if (displayedState && state->contains(displayedState)) {
                displayedState = fallback;
            }

            if (workingState && state->contains(workingState)) {
                workingState = nullptr;
            }

            if (latestCommittedState && state->contains(latestCommittedState)) {
                latestCommittedState = fallback;
            }

            states.erase(
                std::remove_if(
                    states.begin(),
                    states.end(),
                    [state](MaterialState* candidate) {
                        return state->contains(candidate);
                    }
                ),
                states.end()
            );

            state->remove();

            if (!workingState) {
                workingState = MaterialState::FromPriorState(latestCommittedState);
                states.push_back(workingState);
            }

            if (!displayedState) {
                displayedState = workingState;
            }

            return true;
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
            if (!workingState) { return false; }
            if (!workingState->model.changed && !workingState->hasDelta) { return false; }

            workingState->computeDelta();

            workingState->committed = true;
            workingState->working = false;
            workingState->name = "Material State";

            latestCommittedState = workingState;

            workingState = MaterialState::FromPriorState(latestCommittedState);

            states.push_back(workingState);

            displayedState = workingState;

            return true;
        }
    };
}