module;

#include <string>
#include <vector>
#include <algorithm>

#include <dbg.hpp>

export module Cam.App.MaterialState;

import Rev.OS.File;

import Cam.App.Model;
import Cam.App.Tool;
import Cam.App.ToolPath;

export namespace Cam::App {

    struct MaterialState {

        // Geometry
        //--------------------------------------------------

        Model model;

        // Removed material from parent -> this state.
        //
        // delta = parent.model - model
        Model delta;
        bool hasDelta = false;

        // Family
        //--------------------------------------------------

        MaterialState* parent = nullptr;
        std::vector<MaterialState*> children;

        // State
        //--------------------------------------------------

        bool committed = false;
        bool working = false;

        std::string name = "";

        // Tool Path
        //--------------------------------------------------

        ToolPath toolPath;
        bool hasToolPath = false;

        // Construction
        //--------------------------------------------------

        static MaterialState* FromStep(Rev::OS::File& file) {

            MaterialState* state = new MaterialState();

            state->model = Model::FromStep(file);
            state->model.clearSelection();

            state->delta.clear();
            state->hasDelta = false;

            state->parent = nullptr;
            state->children.clear();

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

            state->delta.clear();
            state->hasDelta = false;

            state->parent = prior;
            state->children.clear();

            state->committed = false;
            state->working = true;

            state->name = "Working State";

            prior->children.push_back(state);

            return state;
        }


        // Selection
        //--------------------------------------------------

        void clearSelection() {

            model.clearSelection();
        }

        // Family management
        //--------------------------------------------------

        bool isRoot() const {

            return parent == nullptr;
        }

        bool hasChild(MaterialState* state) const {

            return std::find(
                children.begin(),
                children.end(),
                state
            ) != children.end();
        }

        void addChild(MaterialState* state) {

            if (!state) { return; }
            if (hasChild(state)) { return; }

            children.push_back(state);
            state->parent = this;
        }

        void removeChild(MaterialState* state) {

            if (!state) { return; }

            children.erase(
                std::remove(
                    children.begin(),
                    children.end(),
                    state
                ),
                children.end()
            );

            if (state->parent == this) {
                state->parent = nullptr;
            }
        }

        void detach() {

            if (!parent) { return; }

            parent->removeChild(this);
            parent = nullptr;
        }

        bool contains(MaterialState* state) {

            if (this == state) { return true; }

            for (MaterialState* child : children) {
                if (child && child->contains(state)) { return true; }
            }

            return false;
        }

        void collectSubtree(std::vector<MaterialState*>& out) {

            out.push_back(this);

            for (MaterialState* child : children) {
                if (child) { child->collectSubtree(out); }
            }
        }

        // Destruction
        //--------------------------------------------------

        void remove() {

            detach();

            std::vector<MaterialState*> oldChildren = children;
            children.clear();

            for (MaterialState* child : oldChildren) {

                if (!child) { continue; }

                child->parent = nullptr;
                child->remove();
            }

            model.clear();
            clearDelta();

            delete this;
        }

        // Delta
        //--------------------------------------------------

        void clearDelta() {

            delta.clear();
            hasDelta = false;

            clearToolPath();
        }

        void computeDelta() {

            clearDelta();

            if (!parent) { return; }
            if (!parent->model.loaded) { return; }
            if (!model.loaded) { return; }

            // Removed material = parent state minus current state.
            delta = Model::Difference(
                model,
                parent->model
            );

            hasDelta = delta.loaded;

            if (hasDelta) {
                computeToolPath();
            }
        }

        // Tool Path
        //--------------------------------------------------

        void clearToolPath() {

            toolPath.clear();
            hasToolPath = false;
        }

        void computeToolPath() {

            clearToolPath();

            if (!hasDelta) { return; }
            if (!delta.loaded) { return; }
            if (!parent->model.loaded) { return; }

            Tool tool = Tool::GodTool();

            hasToolPath = toolPath.compute(
                delta,
                parent->model,
                tool
            );
        }
    };
}