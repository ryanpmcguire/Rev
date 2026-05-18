module;

#include <string>
#include <vector>

export module Cam.App.MaterialState;

import Rev.OS.File;

import Cam.App.Model;

export namespace Cam::App {

    struct MaterialState {

        Model model;

        MaterialState* parent = nullptr;
        std::vector<MaterialState*> children;

        bool committed = false;
        bool working = false;

        std::string name = "";

        static MaterialState* FromStep(Rev::OS::File& file) {

            MaterialState* state = new MaterialState();

            state->model = Model::FromStep(file);
            state->model.clearSelection();

            state->parent = nullptr;
            state->committed = true;
            state->working = false;
            state->name = "Final State";

            return state;
        }

        static MaterialState* FromPriorState(MaterialState* prior) {

            if (!prior) {
                return nullptr;
            }

            MaterialState* state = new MaterialState();

            state->model = prior->model;
            state->model.clearSelection();
            state->model.changed = false;

            state->parent = prior;
            state->committed = false;
            state->working = true;
            state->name = "Working State";

            prior->children.push_back(state);

            return state;
        }
    };
};