module;

#include <string>
#include <vector>
#include <algorithm>

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

            if (!prior) { return nullptr; }

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

        void collectSubtree(std::vector<MaterialState*>& out) {

            out.push_back(this);

            for (MaterialState* child : children) {
                if (child) { child->collectSubtree(out); }
            }
        }

        bool contains(MaterialState* state) {

            if (this == state) { return true; }

            for (MaterialState* child : children) {
                if (child && child->contains(state)) { return true; }
            }

            return false;
        }

        void detach() {

            if (!parent) { return; }

            parent->children.erase(
                std::remove(
                    parent->children.begin(),
                    parent->children.end(),
                    this
                ),
                parent->children.end()
            );

            parent = nullptr;
        }

        void remove() {

            detach();

            std::vector<MaterialState*> oldChildren = children;
            children.clear();

            for (MaterialState* child : oldChildren) {

                if (!child) { continue; }

                child->parent = nullptr;
                child->remove();
            }

            delete this;
        }
    };
};