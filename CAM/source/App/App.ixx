module;

#include <vector>

export module Cam.App;

import Rev.OS.File;
import Cam.App.Model;

export namespace Cam::App {

    struct AppState {

        Rev::OS::File file = Rev::OS::File({
            .pathname = "C:/Users/Ryan/Desktop/Ryan/recils/parts/Nut Mount (Cross Mounted) (Chamfered).STEP"
        });

        // Current working / preview model.
        // Delete/defeature operations happen here first.
        Model model;

        // Committed material states.
        // State 0 is the final loaded STEP model and should not be modified.
        std::vector<Model> materialStates;

        // Display management.
        //
        // Normally this points at `model` while working.
        // When the user clicks a committed material state in the UI,
        // this points at materialStates[index].
        Model* displayedModel = nullptr;
        size_t displayedMaterialState = 0;
        bool displayingWorkingModel = true;

        AppState() {

            loadDefaultModel();
        }

        void loadDefaultModel() {

            model.loadStep(file);
            model.clearSelection();

            materialStates.clear();
            materialStates.push_back(model);

            displayedModel = &model;
            displayedMaterialState = 0;
            displayingWorkingModel = true;
        }

        // Display selection
        //--------------------------------------------------

        Model* getDisplayedModel() {

            if (displayedModel) {
                return displayedModel;
            }

            return &model;
        }

        void displayWorkingModel() {

            displayedModel = &model;
            displayingWorkingModel = true;
        }

        void displayMaterialState(size_t index) {

            if (index >= materialStates.size()) {
                return;
            }

            displayedModel = &materialStates[index];
            displayedMaterialState = index;
            displayingWorkingModel = false;
        }

        bool isDisplayingMaterialState(size_t index) const {

            if (displayingWorkingModel) {
                return false;
            }

            return displayedMaterialState == index;
        }

        // Model operations
        //--------------------------------------------------

        bool defeatureSelected() {

            displayWorkingModel();

            bool ok = model.defeatureSelected();

            if (ok) {
                model.clearSelection();
            }

            return ok;
        }

        void commitMaterialState() {

            Model committed = model;
            committed.clearSelection();

            materialStates.push_back(committed);

            displayedMaterialState = materialStates.size() - 1;
            displayMaterialState(displayedMaterialState);

            model.clearSelection();
        }

        size_t materialStateCount() const {

            return materialStates.size();
        }

        Model& materialState(size_t index) {

            return materialStates[index];
        }

        static AppState* Get(void* pState) {

            return static_cast<AppState*>(pState);
        }
    };
}