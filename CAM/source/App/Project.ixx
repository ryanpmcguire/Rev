module;

#include <vector>
#include <algorithm>
#include <string>
#include <fstream>
#include <cstddef>

#include <nlohmann/json.hpp>

export module Cam.App.Project;

import Rev.OS.File;

import Cam.App.Model;
import Cam.App.MaterialState;
import Cam.App.ToolLibrary;
export namespace Cam::App {

    using Json = nlohmann::json;

    struct Project {

        // Identity
        std::string name = "Untitled Project";

        bool loaded = false;
        bool dirty = false;

        // Files
        //--------------------------------------------------

        // The project file: *.cam
        Rev::OS::File projectFile = Rev::OS::File({
            .pathname = ""
        });

        // The source CAD file: *.step / *.stp
        Rev::OS::File file = Rev::OS::File({
            .pathname = ""
        });

        // Folder containing tool JSON files (*.json). Empty uses app default.
        std::string toolFolderPath = "";

        // Tools
        //--------------------------------------------------

        ToolLibrary toolLibrary;

        // Name of the tool used for new toolpath computation (library key).
        std::string selectedToolName = "God Tool 1";

        // Material states
        //--------------------------------------------------

        std::vector<MaterialState*> states;

        MaterialState* rootState = nullptr;
        MaterialState* latestCommittedState = nullptr;
        MaterialState* workingState = nullptr;
        MaterialState* displayedState = nullptr;

        // Create
        //--------------------------------------------------

        Project(bool loadDefault = true, std::string name = "Untitled Project") {
            this->name = name;

            if (loadDefault) {
                loadDefaultModel();
            }
        }

        // Destroy
        //--------------------------------------------------

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

        bool hasProjectFile() const {
            return projectFile.valid && !projectFile.pathname.empty();
        }

        bool hasModel() const {
            return loaded && displayedState;
        }

        void markDirty() {
            dirty = true;
        }

        // Index helpers
        //--------------------------------------------------

        size_t indexOf(MaterialState* state) const {

            for (size_t i = 0; i < states.size(); i++) {
                if (states[i] == state) {
                    return i;
                }
            }

            return static_cast<size_t>(-1);
        }

        MaterialState* stateAt(size_t index) const {

            if (index >= states.size()) { return nullptr; }

            return states[index];
        }

        // Tools
        //--------------------------------------------------

        void loadToolLibrary() {

            ToolLibrary::assignDefaultToolFolderIfNeeded(toolFolderPath);

            toolLibrary.loadOrCreateDefaults(toolFolderPath);

            if (selectedToolName.empty() || !toolLibrary.find(selectedToolName)) {

                if (!toolLibrary.order.empty()) { selectedToolName = toolLibrary.order.front(); }
                else { selectedToolName = "God Tool 1"; }
            }
        }

        // Serialization
        //--------------------------------------------------

        Json getState() const {

            Json json;

            json["type"] = "Cam.Project";
            json["version"] = 1;

            json["name"] = name;
            json["loaded"] = loaded;

            json["sourceFile"] = {
                { "pathname", file.pathname },
                { "name", file.name },
                { "ext", file.ext }
            };

            json["toolFolderPath"] = toolFolderPath;

            json["materialStates"] = Json::array();

            for (size_t i = 0; i < states.size(); i++) {

                MaterialState* state = states[i];

                if (!state) { continue; }

                Json stateJson;

                stateJson["index"] = i;
                stateJson["name"] = state->name;
                stateJson["committed"] = state->committed;
                stateJson["working"] = state->working;
                stateJson["hasDelta"] = state->hasDelta;

                size_t parentIndex = indexOf(state->parent);

                if (parentIndex != static_cast<size_t>(-1)) { stateJson["parent"] = parentIndex; }
                else { stateJson["parent"] = nullptr; }

                stateJson["model"] = state->model.getState();

                if (state->hasDelta) { stateJson["delta"] = state->delta.getState(); }
                else { stateJson["delta"] = ""; }

                stateJson["toolPath"] = {
                    { "toolName", state->toolPath.toolName },
                    { "strategy", state->toolPath.strategy },
                    { "strategyAuto", state->toolPath.strategyAuto },
                    { "stepDown", state->toolPath.stepDown },
                    { "stepover", state->toolPath.stepover },
                    { "feedRate", state->toolPath.feedRate },
                    { "hasToolPath", state->hasToolPath }
                };

                json["materialStates"].push_back(stateJson);
            }

            json["rootState"] = indexOf(rootState);
            json["latestCommittedState"] = indexOf(latestCommittedState);
            json["workingState"] = indexOf(workingState);
            json["displayedState"] = indexOf(displayedState);

            return json;
        }

        bool setState(const Json& json) {
            if (!json.is_object()) { return false; }

            std::vector<MaterialState*> newStates;

            try {

                if (json.contains("name") && json["name"].is_string()) {
                    name = json["name"].get<std::string>();
                }

                if (json.contains("sourceFile") && json["sourceFile"].is_object() && json["sourceFile"].contains("pathname") && json["sourceFile"]["pathname"].is_string()) {
                    file = Rev::OS::File({
                        .pathname = json["sourceFile"]["pathname"].get<std::string>()
                    });
                }

                if (json.contains("toolFolderPath") && json["toolFolderPath"].is_string()) {
                    toolFolderPath = json["toolFolderPath"].get<std::string>();
                }

                if (!json.contains("materialStates") || !json["materialStates"].is_array()) {
                    return false;
                }

                const Json& materialStatesJson = json["materialStates"];

                newStates.resize(materialStatesJson.size(), nullptr);

                // First pass: create states and hydrate model/delta data.
                for (const Json& stateJson : materialStatesJson) {

                    if (!stateJson.contains("index")) {
                        continue;
                    }

                    size_t index = stateJson["index"].get<size_t>();

                    if (index >= newStates.size()) {
                        continue;
                    }

                    MaterialState* state = new MaterialState();

                    if (stateJson.contains("name") && stateJson["name"].is_string()) {
                        state->name = stateJson["name"].get<std::string>();
                    }

                    if (stateJson.contains("committed")) {
                        state->committed = stateJson["committed"].get<bool>();
                    }

                    if (stateJson.contains("working")) {
                        state->working = stateJson["working"].get<bool>();
                    }

                    if (stateJson.contains("hasDelta")) {
                        state->hasDelta = stateJson["hasDelta"].get<bool>();
                    }

                    if (stateJson.contains("model") && stateJson["model"].is_string()) {
                        state->model.setState(stateJson["model"].get<std::string>());
                    }

                    if (state->hasDelta && stateJson.contains("delta") && stateJson["delta"].is_string()) {
                        state->delta.setState(stateJson["delta"].get<std::string>());
                    }

                    if (stateJson.contains("toolPath") && stateJson["toolPath"].is_object()) {
                        const Json& toolPathJson = stateJson["toolPath"];

                        if (toolPathJson.contains("toolName") && toolPathJson["toolName"].is_string()) {
                            state->toolPath.toolName = toolPathJson["toolName"].get<std::string>();
                        }

                        if (toolPathJson.contains("hasToolPath") && toolPathJson["hasToolPath"].is_boolean()) {
                            state->hasToolPath = toolPathJson["hasToolPath"].get<bool>();
                        }

                        if (toolPathJson.contains("strategy") && toolPathJson["strategy"].is_string()) {
                            state->toolPath.strategy = toolPathJson["strategy"].get<std::string>();
                        }

                        if (toolPathJson.contains("strategyAuto") && toolPathJson["strategyAuto"].is_boolean()) {
                            state->toolPath.strategyAuto = toolPathJson["strategyAuto"].get<bool>();
                        }
                        else if (toolPathJson.contains("strategy")) {
                            // Legacy project files: an explicit strategy field
                            // means the user committed to that strategy. Lock
                            // it in so we don't override it via auto-detection.
                            state->toolPath.strategyAuto = false;
                        }

                        if (toolPathJson.contains("stepDown") && toolPathJson["stepDown"].is_number()) {
                            state->toolPath.stepDown = toolPathJson["stepDown"].get<double>();
                        }

                        if (toolPathJson.contains("feedRate") && toolPathJson["feedRate"].is_number()) {
                            state->toolPath.feedRate = toolPathJson["feedRate"].get<double>();
                        }

                        if (toolPathJson.contains("stepover") && toolPathJson["stepover"].is_number()) {
                            state->toolPath.stepover = toolPathJson["stepover"].get<double>();
                        }
                    }

                    newStates[index] = state;
                }

                // Second pass: restore parent links.
                for (const Json& stateJson : materialStatesJson) {

                    if (!stateJson.contains("index")) {
                        continue;
                    }

                    size_t index = stateJson["index"].get<size_t>();

                    if (index >= newStates.size()) {
                        continue;
                    }

                    MaterialState* state = newStates[index];

                    if (!state) { continue; }

                    if (stateJson.contains("parent") && stateJson["parent"].is_number_unsigned()) {
                        size_t parentIndex = stateJson["parent"].get<size_t>();

                        if (parentIndex < newStates.size()) {
                            state->parent = newStates[parentIndex];
                        }
                    }
                }

                MaterialState* newRoot = nullptr;
                MaterialState* newLatestCommitted = nullptr;
                MaterialState* newWorking = nullptr;
                MaterialState* newDisplayed = nullptr;

                if (json.contains("rootState")) {
                    newRoot = stateAtJsonIndex(newStates, json["rootState"]);
                }

                if (json.contains("latestCommittedState")) {
                    newLatestCommitted = stateAtJsonIndex(newStates, json["latestCommittedState"]);
                }

                if (json.contains("workingState")) {
                    newWorking = stateAtJsonIndex(newStates, json["workingState"]);
                }

                if (json.contains("displayedState")) {
                    newDisplayed = stateAtJsonIndex(newStates, json["displayedState"]);
                }

                std::vector<MaterialState*> oldStates = states;

                states = newStates;

                rootState = newRoot;
                latestCommittedState = newLatestCommitted;
                workingState = newWorking;
                displayedState = newDisplayed;

                if (!rootState && !states.empty()) {
                    rootState = states.front();
                }

                if (!latestCommittedState) {
                    latestCommittedState = rootState;
                }

                if (!displayedState) {
                    displayedState = workingState ? workingState : latestCommittedState;
                }

                loaded = rootState != nullptr;
                dirty = false;

                loadToolLibrary();

                ensureToolPathComputed(displayedState);

                for (MaterialState* oldState : oldStates) {
                    delete oldState;
                }

                return true;
            }

            catch (...) {

                for (MaterialState* state : newStates) {
                    delete state;
                }

                return false;
            }
        }

        static MaterialState* stateAtJsonIndex(const std::vector<MaterialState*>& list, const Json& indexJson) {
            if (!indexJson.is_number_unsigned()) { return nullptr; }

            size_t index = indexJson.get<size_t>();

            if (index >= list.size()) { return nullptr; }

            return list[index];
        }

        // Project file saving/loading
        //--------------------------------------------------

        bool save() {

            if (!hasProjectFile()) { return saveAs(); }

            return writeProjectFile(projectFile);
        }

        bool saveAs() {

            Rev::OS::File selected = projectFile;

            if (!selected.saveAs("Save CAM Project", "CAM Project\0*.cam\0JSON Files\0*.json\0All Files\0*.*\0")) { return false; }

            projectFile = selected;

            return writeProjectFile(projectFile);
        }

        bool loadProjectFile(Rev::OS::File selected) {
            if (!selected || selected.pathname.empty()) { return false; }

            std::ifstream stream(selected.pathname);

            if (!stream) { return false; }

            Json json;

            try {
                stream >> json;
            }

            catch (...) {
                return false;
            }

            if (!setState(json)) { return false; }

            projectFile = selected;
            dirty = false;

            return true;
        }

        bool writeProjectFile(Rev::OS::File& target) {
            if (!target || target.pathname.empty()) { return false; }
            if (!target.writeText(getState().dump(4))) { return false; }

            dirty = false;

            return true;
        }

        // File/model loading
        //--------------------------------------------------

        bool selectStepFile() {

            Rev::OS::File selected = file;

            if (!selected.open("Select STEP File", "STEP Files\0*.step;*.stp\0All Files\0*.*\0")) { return false; }

            return loadStepFile(selected);
        }

        bool loadStepFile() {
            return loadStepFile(file);
        }

        bool loadStepFile(Rev::OS::File& selected) {
            if (!selected) { return false; }

            MaterialState* newRoot = MaterialState::FromStep(selected);

            if (!newRoot) { return false; }

            MaterialState* newWorking = MaterialState::FromPriorState(newRoot);

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
            dirty = true;

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

        void ensureToolPathComputed(MaterialState* state) {

            if (!state || !state->needsToolPathComputation()) { return; }

            state->computeToolPath(toolLibrary, selectedToolName);
        }

        bool selectState(MaterialState* state) {

            if (!state) { return false; }

            displayedState = state;
            ensureToolPathComputed(state);

            return true;
        }

        // Material state editing
        //--------------------------------------------------

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

            states.erase(std::remove_if(states.begin(), states.end(), [state](MaterialState* candidate) {
                return state->contains(candidate);
            }), states.end());

            state->remove();

            if (!latestCommittedState) {
                latestCommittedState = rootState;
            }

            if (!workingState && latestCommittedState) {

                workingState = MaterialState::FromPriorState(latestCommittedState);

                if (workingState) {
                    states.push_back(workingState);
                }
            }

            if (!displayedState) {
                displayedState = workingState ? workingState : latestCommittedState;
            }

            loaded = rootState != nullptr;
            dirty = true;

            return true;
        }

        bool defeatureSelected() {

            if (!workingState) { return false; }

            displayedState = workingState;

            bool ok = workingState->model.defeatureSelected();

            if (!ok) { return false; }

            workingState->model.clearSelection();
            workingState->computeDelta(toolLibrary, selectedToolName);

            dirty = true;

            return true;
        }

        bool commitWorkingState() {

            if (!workingState) { return false; }
            if (!workingState->model.changed && !workingState->hasDelta) { return false; }

            workingState->computeDelta(toolLibrary, selectedToolName);

            workingState->committed = true;
            workingState->working = false;
            workingState->name = "Material State";

            latestCommittedState = workingState;

            workingState = MaterialState::FromPriorState(latestCommittedState);

            if (workingState) {
                states.push_back(workingState);
            }

            displayedState = workingState ? workingState : latestCommittedState;

            loaded = rootState != nullptr;
            dirty = true;

            return true;
        }

        bool recalculateDisplayedToolPath() {

            if (!displayedState) { return false; }
            if (!displayedState->parent) { return false; }

            displayedState->computeDelta(toolLibrary, selectedToolName);

            dirty = true;

            return displayedState->hasToolPath;
        }
    };
}