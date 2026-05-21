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

        bool loaded = false;

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

        // State
        //--------------------------------------------------

        void clear() {

            for (MaterialState* state : states) {
                delete state;
            }

            states.clear();

            rootState = nullptr;
            latestCommittedState = nullptr;
            workingState = nullptr;
            displayedState = nullptr;

            loaded = false;
        }

        bool empty() const {
            return states.empty() || !rootState;
        }

        bool hasFile() const {
            return file.valid && !file.pathname.empty();
        }

        bool hasModel() const {
            return loaded && displayedState;
        }

        // File/model loading
        //--------------------------------------------------

        bool selectStepFile() {

            Rev::OS::File selected = file;

            if (!selected.open(
                "Select STEP File",
                "STEP Files\0*.step;*.stp\0All Files\0*.*\0"
            )) {
                return false;
            }

            return loadStepFile(selected);
        }

        bool loadStepFile() {
            return loadStepFile(file);
        }

        bool loadStepFile(
            Rev::OS::File& selected
        ) {
            if (!selected) {
                return false;
            }

            // Build the replacement state tree before touching the current one.
            MaterialState* newRoot = MaterialState::FromStep(selected);

            if (!newRoot) {
                return false;
            }

            MaterialState* newWorking = MaterialState::FromPriorState(newRoot);

            // Only after successful construction do we preserve the old tree
            // and swap the project over to the new tree.
            std::vector<MaterialState*> oldStates = states;

            states.clear();

            file = selected;

            if (!file.name.empty()) {
                name = file.name;
            }

            rootState = newRoot;
            latestCommittedState = newRoot;
            workingState = newWorking;
            displayedState = newWorking ? newWorking : newRoot;

            states.push_back(rootState);

            if (workingState) {
                states.push_back(workingState);
            }

            loaded = true;

            // Now it is safe to delete the old states.
            for (MaterialState* state : oldStates) {
                delete state;
            }

            return true;
        }

        void loadDefaultModel() {

            file = DefaultFile();
            name = "Nut Mount";

            loadStepFile(file);
        }

        // Access
        //--------------------------------------------------

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

            loaded = rootState != nullptr;

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

            loaded = rootState != nullptr;

            return true;
        }
    };
}