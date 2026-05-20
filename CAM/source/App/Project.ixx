module;

#include <vector>
#include <algorithm>
#include <string>

export module Cam.App.Project;

import Rev.OS.File;

import Cam.App.Model;
import Cam.App.MaterialState;

export namespace Cam::App {

    struct Project {

        std::string name = "Untitled Project";

        Rev::OS::File file = Rev::OS::File({
            .pathname = ""
        });

        std::vector<MaterialState*> states;

        MaterialState* rootState = nullptr;
        MaterialState* latestCommittedState = nullptr;
        MaterialState* workingState = nullptr;
        MaterialState* displayedState = nullptr;

        Project(
            bool loadDefault = true,
            std::string name = "Untitled Project"
        ) {
            this->name = name;

            if (loadDefault) {
                loadDefaultModel();
            }
        }

        ~Project() {
            clear();
        }

        static Rev::OS::File DefaultFile() {
            return Rev::OS::File({
                .pathname = "C:/Users/Ryan/Desktop/Ryan/recils/parts/Nut Mount (Cross Mounted) (Chamfered).STEP"
            });
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

        bool empty() const {
            return states.empty() || !rootState;
        }

        bool hasModel() const {
            return displayedState != nullptr;
        }

        void loadDefaultModel() {

            clear();

            name = "Nut Mount";
            file = DefaultFile();

            rootState = MaterialState::FromStep(file);

            if (!rootState) { return; }

            states.push_back(rootState);

            latestCommittedState = rootState;

            workingState = MaterialState::FromPriorState(rootState);

            if (workingState) {
                states.push_back(workingState);
            }

            displayedState = workingState ? workingState : rootState;
        }

        Model* getDisplayedModel() {

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

            if (!latestCommittedState) {
                latestCommittedState = rootState;
            }

            if (!workingState && latestCommittedState) {

                workingState = MaterialState::FromPriorState(
                    latestCommittedState
                );

                if (workingState) {
                    states.push_back(workingState);
                }
            }

            if (!displayedState) {
                displayedState = workingState ? workingState : latestCommittedState;
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

            workingState = MaterialState::FromPriorState(
                latestCommittedState
            );

            if (workingState) {
                states.push_back(workingState);
            }

            displayedState = workingState ? workingState : latestCommittedState;

            return true;
        }
    };
}