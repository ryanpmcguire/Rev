module;

#include <string>
#include <vector>
#include <algorithm>

#include <dbg.hpp>

export module Cam.App.MaterialState;

import Rev.OS.File;

import Cam.App.Model;
import Cam.App.Tool;
import Cam.App.ToolLibrary;
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

        // True for the auto-generated stock-definition states (the four faces
        // that grow the final prism out to the defined raw stock).  These are
        // regenerated when stock parameters change, but remain fully editable
        // (tool / toolpath settings) like any other material state.
        bool stockGenerated = false;

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

            return std::find(children.begin(), children.end(), state) != children.end();
        }

        void addChild(MaterialState* state) {

            if (!state) { return; }
            if (hasChild(state)) { return; }

            children.push_back(state);
            state->parent = this;
        }

        void removeChild(MaterialState* state) {

            if (!state) { return; }

            auto it = std::find(children.begin(), children.end(), state);

            if (it == children.end()) { return; }

            children.erase(it);

            if (state->parent == this) {
                state->parent = nullptr;
            }
        }

        void detach() {

            MaterialState* owner = parent;

            if (!owner) { return; }

            parent = nullptr;
            owner->removeChild(this);
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

        void computeDelta(const ToolLibrary& library, const std::string& activeToolName) {

            clearDelta();

            if (!parent) { return; }
            if (!parent->model.loaded) { return; }
            if (!model.loaded) { return; }

            // Removed material = parent state minus current state.
            delta = Model::Difference(model, parent->model);

            hasDelta = delta.loaded;

            if (hasDelta) {
                computeToolPath(library, activeToolName);
            }
        }

        // Tool Path
        //--------------------------------------------------

        void clearToolPath() {

            toolPath.clear();
            hasToolPath = false;
        }

        void computeToolPath(const ToolLibrary& library, const std::string& activeToolName) {

            toolPath.clearPathData();
            hasToolPath = false;

            if (!hasDelta) { return; }
            if (!delta.loaded) { return; }
            if (!parent->model.loaded) { return; }

            if (toolPath.toolName.empty()) {
                toolPath.toolName = activeToolName;
            }

            const Tool* tool = library.find(toolPath.toolName);

            if (!tool && !activeToolName.empty()) {
                toolPath.toolName = activeToolName;
                tool = library.find(toolPath.toolName);
            }

            if (!tool) {
                dbg(
                    "[MaterialState] Tool \"%s\" not in library",
                    toolPath.toolName.c_str()
                );
                return;
            }

            hasToolPath = toolPath.compute(delta, parent->model, *tool);
        }

        bool link(MaterialState* nextMaterialState) {

            if (!nextMaterialState) { return false; }

            if (!hasToolPath || !nextMaterialState->hasToolPath) {
                return false;
            }

            if (toolPath.points.empty() || nextMaterialState->toolPath.points.empty()) {
                return false;
            }

            // Pass the user-defined stock centre and rotary axis so the link
            // generates a proper polar arc rather than a straight-line move.
            // Both are propagated to all states, so reading from this model is fine.
            if (model.hasAxisOrigin && model.hasAxisX) {
                return toolPath.link(
                    nextMaterialState->toolPath,
                    model.axisOrigin,
                    model.axisXDirection
                );
            }

            return toolPath.link(nextMaterialState->toolPath);
        }

        bool needsToolPathComputation() const {

            if (!hasDelta || !delta.loaded || !parent) {
                return false;
            }

            return !hasToolPath || !toolPath.computed || toolPath.points.empty();
        }
    };
}