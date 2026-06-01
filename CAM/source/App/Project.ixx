module;

#include <vector>
#include <algorithm>
#include <string>
#include <fstream>
#include <cstddef>
#include <cmath>

#include <TopoDS_Shape.hxx>

#include <nlohmann/json.hpp>
#include <dbg.hpp>

export module Cam.App.Project;

import Rev.OS.File;
import Rev.Core.Pos3;

import Cam.App.Model;
import Cam.App.MaterialState;
import Cam.App.ToolPath;
import Cam.App.ToolLibrary;
export namespace Cam::App {

    using Json = nlohmann::json;

    enum class StockType {
        RectangularPrism,
        Cylinder
    };

    // User-defined raw stock that the final rectangular prism is machined from.
    // The stock axis is always the part's X / rotary axis and its length equals
    // the part length; only the cross-section (rectangular W×H or a radius) is
    // user-controlled.
    struct StockDefinition {
        bool defined = false;       // stock is active (menu engaged)
        bool initialized = false;   // defaults computed from the part yet
        StockType type = StockType::RectangularPrism;
        double radius = 0.0;        // cylinder cross-section radius
        double width = 0.0;         // prism full extent along axis Y
        double height = 0.0;        // prism full extent along axis Z
        double length = 0.0;        // extent along the axis (== part length)

        void reset() {
            *this = StockDefinition();
        }
    };

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
        std::vector<MaterialState*> viewSelection;

        // Raw stock definition (and its auto-generated states).
        StockDefinition stock;

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
            viewSelection.clear();

            stock.reset();

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

        // Rebuild parent/child links from the flat states list (index order is history).
        void relinkMaterialStateHierarchy() {

            for (MaterialState* node : states) {

                if (!node) { continue; }

                node->parent = nullptr;
                node->children.clear();
            }

            if (states.empty()) {

                rootState = nullptr;
                latestCommittedState = nullptr;
                workingState = nullptr;

                return;
            }

            rootState = states.front();

            for (size_t i = 1; i < states.size(); i++) {

                MaterialState* prior = states[i - 1];
                MaterialState* node = states[i];

                if (!prior || !node) { continue; }

                prior->addChild(node);
            }

            latestCommittedState = rootState;
            workingState = nullptr;

            for (MaterialState* node : states) {

                if (!node) { continue; }

                if (node->working) {
                    workingState = node;
                }

                if (node->committed && !node->working) {
                    latestCommittedState = node;
                }
            }

            if (!latestCommittedState) {
                latestCommittedState = rootState;
            }
        }

        void sortViewSelection() {

            std::sort(
                viewSelection.begin(),
                viewSelection.end(),
                [this](MaterialState* a, MaterialState* b) {
                    return indexOf(a) < indexOf(b);
                }
            );
        }

        MaterialState* primaryViewState() const {

            MaterialState* primary = nullptr;

            for (MaterialState* state : viewSelection) {

                if (!state) { continue; }

                if (!primary || indexOf(state) < indexOf(primary)) {
                    primary = state;
                }
            }

            if (primary) { return primary; }

            return displayedState;
        }

        bool isViewSelected(MaterialState* state) const {

            if (!state) { return false; }

            return std::find(
                viewSelection.begin(),
                viewSelection.end(),
                state
            ) != viewSelection.end();
        }

        void syncViewSelectionToDisplayed() {

            viewSelection.clear();

            if (displayedState) {
                viewSelection.push_back(displayedState);
            }
        }

        void pruneViewSelection() {

            viewSelection.erase(
                std::remove_if(
                    viewSelection.begin(),
                    viewSelection.end(),
                    [this](MaterialState* candidate) {
                        return !candidate || indexOf(candidate) == static_cast<size_t>(-1);
                    }
                ),
                viewSelection.end()
            );

            if (viewSelection.empty()) {
                syncViewSelectionToDisplayed();
            }
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
                stateJson["stockGenerated"] = state->stockGenerated;
                stateJson["hasDelta"] = state->hasDelta;

                size_t parentIndex = indexOf(state->parent);

                if (parentIndex != static_cast<size_t>(-1)) { stateJson["parent"] = parentIndex; }
                else { stateJson["parent"] = nullptr; }

                stateJson["model"] = state->model.getState();

                if (state->hasDelta) { stateJson["delta"] = state->delta.getState(); }
                else { stateJson["delta"] = ""; }

                // Axis frame (stock center + orientation).
                // Saved per-state so every setup can carry its own frame,
                // and propagateAxisToAllStates keeps them in sync at runtime.
                stateJson["axisOrigin"] = Json::array({
                    state->model.axisOrigin.x,
                    state->model.axisOrigin.y,
                    state->model.axisOrigin.z
                });
                stateJson["hasAxisOrigin"] = state->model.hasAxisOrigin;

                stateJson["axisXDirection"] = Json::array({
                    state->model.axisXDirection.x,
                    state->model.axisXDirection.y,
                    state->model.axisXDirection.z
                });
                stateJson["hasAxisX"] = state->model.hasAxisX;

                stateJson["axisYDirection"] = Json::array({
                    state->model.axisYDirection.x,
                    state->model.axisYDirection.y,
                    state->model.axisYDirection.z
                });
                stateJson["hasAxisY"] = state->model.hasAxisY;

                stateJson["axisZDirection"] = Json::array({
                    state->model.axisZDirection.x,
                    state->model.axisZDirection.y,
                    state->model.axisZDirection.z
                });
                stateJson["hasAxisZ"] = state->model.hasAxisZ;

                stateJson["toolPath"] = {
                    { "toolName", state->toolPath.toolName },
                    { "strategy", state->toolPath.strategy },
                    { "strategyAuto", state->toolPath.strategyAuto },
                    { "stepDown", state->toolPath.stepDown },
                    { "stepover", state->toolPath.stepover },
                    { "feedRate", state->toolPath.feedRate },
                    { "rapidSpeedMmPerSec", state->toolPath.rapidSpeedMmPerSec },
                    { "climbMilling", state->toolPath.climbMilling },
                    { "linkRetractDistance", state->toolPath.linkRetractDistance },
                    { "sliceAxis", Json::array({
                        state->toolPath.sliceAxis.x,
                        state->toolPath.sliceAxis.y,
                        state->toolPath.sliceAxis.z
                    }) },
                    { "sliceOrigin", Json::array({
                        state->toolPath.sliceOrigin.x,
                        state->toolPath.sliceOrigin.y,
                        state->toolPath.sliceOrigin.z
                    }) },
                    { "sliceFaceId", state->toolPath.hasSliceFace()
                        ? Json(state->toolPath.sliceFaceId)
                        : Json(nullptr)
                    },
                    { "hasToolPath", state->hasToolPath }
                };

                json["materialStates"].push_back(stateJson);
            }

            json["rootState"] = indexOf(rootState);
            json["latestCommittedState"] = indexOf(latestCommittedState);
            json["workingState"] = indexOf(workingState);
            json["displayedState"] = indexOf(displayedState);

            json["stock"] = {
                { "defined", stock.defined },
                { "initialized", stock.initialized },
                { "type", stock.type == StockType::Cylinder ? "cylinder" : "prism" },
                { "radius", stock.radius },
                { "width", stock.width },
                { "height", stock.height },
                { "length", stock.length }
            };

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

                    if (stateJson.contains("stockGenerated")) {
                        state->stockGenerated = stateJson["stockGenerated"].get<bool>();
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

                    // Restore axis frame.
                    auto readVec3 = [&](const char* key, Rev::Core::Pos3& out) {
                        if (
                            stateJson.contains(key) &&
                            stateJson[key].is_array() &&
                            stateJson[key].size() >= 3
                        ) {
                            out.x = stateJson[key][0].get<float>();
                            out.y = stateJson[key][1].get<float>();
                            out.z = stateJson[key][2].get<float>();
                        }
                    };

                    readVec3("axisOrigin",     state->model.axisOrigin);
                    readVec3("axisXDirection", state->model.axisXDirection);
                    readVec3("axisYDirection", state->model.axisYDirection);
                    readVec3("axisZDirection", state->model.axisZDirection);

                    if (stateJson.contains("hasAxisOrigin") && stateJson["hasAxisOrigin"].is_boolean()) {
                        state->model.hasAxisOrigin = stateJson["hasAxisOrigin"].get<bool>();
                    }
                    if (stateJson.contains("hasAxisX") && stateJson["hasAxisX"].is_boolean()) {
                        state->model.hasAxisX = stateJson["hasAxisX"].get<bool>();
                    }
                    if (stateJson.contains("hasAxisY") && stateJson["hasAxisY"].is_boolean()) {
                        state->model.hasAxisY = stateJson["hasAxisY"].get<bool>();
                    }
                    if (stateJson.contains("hasAxisZ") && stateJson["hasAxisZ"].is_boolean()) {
                        state->model.hasAxisZ = stateJson["hasAxisZ"].get<bool>();
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

                        if (toolPathJson.contains("rapidSpeedMmPerSec") && toolPathJson["rapidSpeedMmPerSec"].is_number()) {
                            state->toolPath.rapidSpeedMmPerSec = toolPathJson["rapidSpeedMmPerSec"].get<double>();
                        }

                        if (toolPathJson.contains("climbMilling") && toolPathJson["climbMilling"].is_boolean()) {
                            state->toolPath.climbMilling = toolPathJson["climbMilling"].get<bool>();
                        }

                        if (toolPathJson.contains("linkRetractDistance") && toolPathJson["linkRetractDistance"].is_number()) {
                            state->toolPath.linkRetractDistance = static_cast<float>(
                                toolPathJson["linkRetractDistance"].get<double>()
                            );
                        }

                        if (toolPathJson.contains("sliceAxis") && toolPathJson["sliceAxis"].is_array() && toolPathJson["sliceAxis"].size() >= 3) {
                            state->toolPath.sliceAxis.x = toolPathJson["sliceAxis"][0].get<float>();
                            state->toolPath.sliceAxis.y = toolPathJson["sliceAxis"][1].get<float>();
                            state->toolPath.sliceAxis.z = toolPathJson["sliceAxis"][2].get<float>();
                        }

                        if (toolPathJson.contains("sliceOrigin") && toolPathJson["sliceOrigin"].is_array() && toolPathJson["sliceOrigin"].size() >= 3) {
                            state->toolPath.sliceOrigin.x = toolPathJson["sliceOrigin"][0].get<float>();
                            state->toolPath.sliceOrigin.y = toolPathJson["sliceOrigin"][1].get<float>();
                            state->toolPath.sliceOrigin.z = toolPathJson["sliceOrigin"][2].get<float>();
                        }

                        if (toolPathJson.contains("sliceFaceId") && !toolPathJson["sliceFaceId"].is_null()) {
                            state->toolPath.sliceFaceId = toolPathJson["sliceFaceId"].get<size_t>();
                        }
                        else {
                            state->toolPath.sliceFaceId = ToolPath::NoSliceFaceId;
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

                relinkMaterialStateHierarchy();

                syncViewSelectionToDisplayed();

                loaded = rootState != nullptr;
                dirty = false;

                stock.reset();

                if (json.contains("stock") && json["stock"].is_object()) {

                    const Json& stockJson = json["stock"];

                    if (stockJson.contains("defined") && stockJson["defined"].is_boolean()) {
                        stock.defined = stockJson["defined"].get<bool>();
                    }
                    if (stockJson.contains("initialized") && stockJson["initialized"].is_boolean()) {
                        stock.initialized = stockJson["initialized"].get<bool>();
                    }
                    if (stockJson.contains("type") && stockJson["type"].is_string()) {
                        stock.type = stockJson["type"].get<std::string>() == "cylinder"
                            ? StockType::Cylinder
                            : StockType::RectangularPrism;
                    }
                    if (stockJson.contains("radius") && stockJson["radius"].is_number()) {
                        stock.radius = stockJson["radius"].get<double>();
                    }
                    if (stockJson.contains("width") && stockJson["width"].is_number()) {
                        stock.width = stockJson["width"].get<double>();
                    }
                    if (stockJson.contains("height") && stockJson["height"].is_number()) {
                        stock.height = stockJson["height"].get<double>();
                    }
                    if (stockJson.contains("length") && stockJson["length"].is_number()) {
                        stock.length = stockJson["length"].get<double>();
                    }
                }

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
            syncViewSelectionToDisplayed();

            states.push_back(rootState);

            if (workingState) {
                states.push_back(workingState);
            }

            relinkMaterialStateHierarchy();

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

        // Connect each state's toolpath to the previous one in history (lower index).
        // prior = states[i] (earlier operation), next = states[i-1] (later operation).
        // Adds a rapid link move from prior's retract point to next's approach point
        // so the preview plays as one continuous motion with correct timing.
        void linkMaterialStateToolPaths() {

            for (MaterialState* state : states) {
                ensureToolPathComputed(state);
            }

            for (size_t i = 1; i < states.size(); i++) {

                MaterialState* prior = states[i];
                MaterialState* next  = states[i - 1];

                if (!prior || !next) { continue; }

                prior->link(next);
            }
        }

        bool selectState(MaterialState* state, bool addToSelection = false) {

            if (!state) { return false; }

            if (!addToSelection) {
                viewSelection = { state };
            }
            else if (!isViewSelected(state)) {
                viewSelection.push_back(state);
                sortViewSelection();
            }

            displayedState = state;

            for (MaterialState* selected : viewSelection) {
                ensureToolPathComputed(selected);
            }

            return true;
        }

        // States are stored with index 0 = most recent; higher index = earlier in time.
        // Forward preview advances toward lower indices.
        MaterialState* nextStateAfterViewSelection() const {

            if (states.empty()) { return nullptr; }

            size_t anchorIndex = static_cast<size_t>(-1);

            for (MaterialState* state : viewSelection) {

                if (!state) { continue; }

                const size_t index = indexOf(state);

                if (index == static_cast<size_t>(-1)) { continue; }

                if (
                    anchorIndex == static_cast<size_t>(-1) ||
                    index < anchorIndex
                ) {
                    anchorIndex = index;
                }
            }

            if (anchorIndex == static_cast<size_t>(-1)) {

                if (!displayedState) { return nullptr; }

                anchorIndex = indexOf(displayedState);

                if (anchorIndex == static_cast<size_t>(-1)) { return nullptr; }
            }

            if (anchorIndex == 0) { return nullptr; }

            return states[anchorIndex - 1];
        }

        // Earlier material state before the highest index in viewSelection (forward-time back).
        MaterialState* previousStateBeforeViewSelection() const {

            if (states.empty()) { return nullptr; }

            size_t anchorIndex = static_cast<size_t>(-1);

            for (MaterialState* state : viewSelection) {

                if (!state) { continue; }

                const size_t index = indexOf(state);

                if (index == static_cast<size_t>(-1)) { continue; }

                if (
                    anchorIndex == static_cast<size_t>(-1) ||
                    index > anchorIndex
                ) {
                    anchorIndex = index;
                }
            }

            if (anchorIndex == static_cast<size_t>(-1)) {

                if (!displayedState) { return nullptr; }

                anchorIndex = indexOf(displayedState);

                if (anchorIndex == static_cast<size_t>(-1)) { return nullptr; }
            }

            if (anchorIndex + 1 >= states.size()) { return nullptr; }

            return states[anchorIndex + 1];
        }

        // Material state editing
        //--------------------------------------------------

        bool deleteState(MaterialState* state) {

            if (!state) { return false; }

            size_t index = indexOf(state);

            if (index == static_cast<size_t>(-1)) { return false; }
            if (index == 0) { return false; }

            MaterialState* anchor = states[index - 1];

            std::vector<MaterialState*> toDelete(
                states.begin() + static_cast<std::ptrdiff_t>(index),
                states.end()
            );

            // Deleting any generated stock state tears down the whole stock,
            // so reset the definition and re-offer the "Generate Stock" button.
            bool removedStock = false;

            for (MaterialState* node : toDelete) {
                if (node && node->stockGenerated) { removedStock = true; break; }
            }

            if (removedStock) {
                stock.defined = false;
                stock.initialized = false;
            }

            auto isRemoved = [&toDelete](MaterialState* candidate) {

                if (!candidate) { return true; }

                return std::find(toDelete.begin(), toDelete.end(), candidate) != toDelete.end();
            };

            if (displayedState && isRemoved(displayedState)) {
                displayedState = anchor;
            }

            if (workingState && isRemoved(workingState)) {
                workingState = nullptr;
            }

            if (latestCommittedState && isRemoved(latestCommittedState)) {
                latestCommittedState = anchor;
            }

            viewSelection.erase(
                std::remove_if(
                    viewSelection.begin(),
                    viewSelection.end(),
                    [&](MaterialState* candidate) { return isRemoved(candidate); }
                ),
                viewSelection.end()
            );

            states.erase(
                states.begin() + static_cast<std::ptrdiff_t>(index),
                states.end()
            );

            for (MaterialState* node : toDelete) {

                node->parent = nullptr;
                node->children.clear();
                node->model.clear();
                node->clearDelta();

                delete node;
            }

            relinkMaterialStateHierarchy();

            if (!workingState && latestCommittedState) {

                workingState = MaterialState::FromPriorState(latestCommittedState);

                if (workingState) {
                    states.push_back(workingState);
                    relinkMaterialStateHierarchy();
                }
            }

            if (!displayedState) {
                displayedState = workingState ? workingState : latestCommittedState;
            }

            pruneViewSelection();

            if (workingState) {
                displayedState = workingState;
            }
            else if (!displayedState) {
                displayedState = latestCommittedState;
            }

            syncViewSelectionToDisplayed();

            loaded = rootState != nullptr;
            dirty = true;

            return true;
        }

        // Stock definition
        //--------------------------------------------------

        // The deepest user (non-stock) state — the rectangular prism the stock
        // grows out from.
        MaterialState* stockBaseState() const {

            if (workingState && !workingState->stockGenerated) {
                return workingState;
            }

            for (auto it = states.rbegin(); it != states.rend(); ++it) {
                if (*it && !(*it)->stockGenerated) { return *it; }
            }

            return nullptr;
        }

        // Stock can be defined once the base part is literally a box.
        bool stockMenuAvailable() {

            MaterialState* base = stockBaseState();

            return base && base->model.isRectangularPrism();
        }

        // Axis-frame extents (X along axis, Y/Z cross-section) of the base part.
        bool partFrameExtents(double& extX, double& extY, double& extZ) {

            MaterialState* base = stockBaseState();

            if (!base) { return false; }

            Rev::Core::Pos3 x, y, z;
            base->model.getOrthonormalAxisFrame(x, y, z);

            double x0, x1, y0, y1, z0, z1;
            base->model.frameBounds({ 0.0f, 0.0f, 0.0f }, x, y, z, x0, x1, y0, y1, z0, z1);

            extX = x1 - x0;
            extY = y1 - y0;
            extZ = z1 - z0;

            return true;
        }

        // True once stock has been generated.
        bool stockGenerated() const {

            for (MaterialState* s : states) {
                if (s && s->stockGenerated) { return true; }
            }

            return false;
        }

        // Detach and delete an (empty) working-copy state, splicing it out of the
        // history without spawning a replacement working state.
        void removeWorkingCopyState(MaterialState* s) {

            if (!s) { return; }

            if (s->parent) {
                auto& children = s->parent->children;
                children.erase(
                    std::remove(children.begin(), children.end(), s),
                    children.end()
                );
            }

            states.erase(std::remove(states.begin(), states.end(), s), states.end());

            if (displayedState == s)        { displayedState = s->parent; }
            if (latestCommittedState == s)  { latestCommittedState = s->parent; }

            viewSelection.erase(
                std::remove(viewSelection.begin(), viewSelection.end(), s),
                viewSelection.end()
            );

            s->parent = nullptr;
            s->children.clear();
            s->model.clear();
            s->clearDelta();

            delete s;

            if (!displayedState) {
                displayedState = latestCommittedState ? latestCommittedState : rootState;
            }

            if (viewSelection.empty()) {
                syncViewSelectionToDisplayed();
            }
        }

        // One-time stock generation, driven by the "Generate Stock" button.
        // Consumes the current working state (so no vestigial editable copy is
        // left parenting the stock chain), fills default dimensions from the
        // part bounding box, and builds the four stock states.
        bool generateStock() {

            if (stock.defined) { return false; }
            if (!stockMenuAvailable()) { return false; }

            MaterialState* base = stockBaseState();

            if (!base) { return false; }

            // Make sure the prism is a permanent, non-editable boundary rather
            // than a trailing working state that would orphan the stock chain.
            if (base == workingState) {

                const bool emptyCopy =
                    base->parent &&
                    !base->hasDelta &&
                    !base->model.changed;

                if (emptyCopy) {
                    // The part was already a prism; drop the redundant copy and
                    // build stock off the committed parent prism.
                    MaterialState* parent = base->parent;
                    removeWorkingCopyState(base);
                    base = parent;
                }
                else {
                    // A real operation produced the prism; finalize it in place.
                    base->working = false;
                    base->committed = true;
                    latestCommittedState = base;
                }

                workingState = nullptr;
            }

            double extX = 0.0, extY = 0.0, extZ = 0.0;

            if (!partFrameExtents(extX, extY, extZ)) { return false; }

            const double margin = 2.0;

            stock.type   = StockType::RectangularPrism;
            stock.length = extX;
            stock.width  = extY + 2.0 * margin;
            stock.height = extZ + 2.0 * margin;
            stock.radius = 0.5 * std::sqrt(extY * extY + extZ * extZ) + margin;

            stock.defined = true;
            stock.initialized = true;

            if (displayedState == nullptr) {
                displayedState = base;
                syncViewSelectionToDisplayed();
            }

            regenerateStockStates();

            return true;
        }

        void copyAxisFrame(const Model& src, Model& dst) {

            dst.axisOrigin    = src.axisOrigin;    dst.hasAxisOrigin = src.hasAxisOrigin;
            dst.axisXDirection = src.axisXDirection; dst.hasAxisX = src.hasAxisX;
            dst.axisYDirection = src.axisYDirection; dst.hasAxisY = src.hasAxisY;
            dst.axisZDirection = src.axisZDirection; dst.hasAxisZ = src.hasAxisZ;
        }

        // Remove and delete all auto-generated stock states, restoring the
        // user chain to its pristine pre-stock form.
        void removeStockStates() {

            std::vector<MaterialState*> stockStates;
            std::vector<MaterialState*> kept;

            for (MaterialState* s : states) {
                if (s && s->stockGenerated) { stockStates.push_back(s); }
                else { kept.push_back(s); }
            }

            if (stockStates.empty()) { return; }

            states = kept;

            // Drop child links from kept states into the (deleted) stock chain.
            for (MaterialState* s : kept) {

                if (!s) { continue; }

                s->children.erase(
                    std::remove_if(
                        s->children.begin(),
                        s->children.end(),
                        [](MaterialState* c) { return c && c->stockGenerated; }
                    ),
                    s->children.end()
                );
            }

            for (MaterialState* s : stockStates) {

                if (displayedState == s) { displayedState = nullptr; }

                s->parent = nullptr;
                s->children.clear();
                s->model.clear();
                s->clearDelta();

                delete s;
            }

            viewSelection.erase(
                std::remove_if(
                    viewSelection.begin(),
                    viewSelection.end(),
                    [&](MaterialState* c) {
                        return std::find(kept.begin(), kept.end(), c) == kept.end();
                    }
                ),
                viewSelection.end()
            );

            if (!displayedState) {
                displayedState = workingState ? workingState : latestCommittedState;
            }

            if (viewSelection.empty()) {
                syncViewSelectionToDisplayed();
            }
        }

        // Rebuild the four stock states from the current stock definition.
        void regenerateStockStates() {

            removeStockStates();

            if (!stock.defined) {
                dirty = true;
                return;
            }

            MaterialState* base = stockBaseState();

            if (!base || !base->model.isRectangularPrism()) {
                dirty = true;
                return;
            }

            Model::StockParams sp;
            sp.cylinder   = (stock.type == StockType::Cylinder);
            sp.radius     = stock.radius;
            sp.halfWidth  = stock.width  * 0.5;
            sp.halfHeight = stock.height * 0.5;

            std::vector<TopoDS_Shape> shapes = base->model.buildStockStepShapes(sp);

            if (shapes.empty()) {
                dbg("[Stock] failed: buildStockStepShapes produced no solids");
                dirty = true;
                return;
            }

            // Slice axis for each generated state = the outward normal of the
            // face being extended.  Pre-setting it means the auto-generated
            // toolpaths already cut along the correct face — no manual fixup.
            Rev::Core::Pos3 fx, fy, fz;
            base->model.getOrthonormalAxisFrame(fx, fy, fz);

            double x0, x1, y0, y1, z0, z1;
            base->model.frameBounds({ 0.0f, 0.0f, 0.0f }, fx, fy, fz, x0, x1, y0, y1, z0, z1);

            const Rev::Core::Pos3 worldCenter =
                fx * float((x0 + x1) * 0.5) +
                fy * float((y0 + y1) * 0.5) +
                fz * float((z0 + z1) * 0.5);

            const Rev::Core::Pos3 sliceAxes[4] = {
                fy, fy * -1.0f, fz, fz * -1.0f
            };

            static const char* faceNames[4] = {
                "Stock (+Y)", "Stock (-Y)", "Stock (+Z)", "Stock (-Z)"
            };

            MaterialState* parent = base;

            for (size_t i = 0; i < shapes.size(); i++) {

                MaterialState* s = new MaterialState();

                s->model = Model::FromShape(shapes[i]);

                if (!s->model.loaded) {
                    dbg("[Stock] step %zu produced an unusable solid; skipping", i);
                    delete s;
                    continue;
                }

                copyAxisFrame(base->model, s->model);

                s->parent = parent;
                parent->children.push_back(s);

                s->committed = true;
                s->working = false;
                s->stockGenerated = true;

                if (i < 4) {
                    s->name = faceNames[i];
                    s->toolPath.sliceAxis = sliceAxes[i];
                    s->toolPath.sliceOrigin = worldCenter;
                }
                else {
                    s->name = "Stock";
                }

                states.push_back(s);

                s->computeDelta(toolLibrary, selectedToolName);

                parent = s;
            }

            dirty = true;
        }

        bool defeatureSelected() {

            if (!workingState) { return false; }

            displayedState = workingState;
            syncViewSelectionToDisplayed();

            bool ok = workingState->model.defeatureSelected();

            if (!ok) { return false; }

            workingState->model.clearSelection();
            workingState->computeDelta(toolLibrary, selectedToolName);

            dirty = true;

            return true;
        }

        bool extendSelected(double distance = 10.0) {

            if (!workingState) {
                dbg("[Extend] failed: no working material state");
                return false;
            }

            displayedState = workingState;
            syncViewSelectionToDisplayed();

            bool ok = workingState->model.extendSelected(distance);

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

            relinkMaterialStateHierarchy();

            displayedState = workingState ? workingState : latestCommittedState;
            syncViewSelectionToDisplayed();

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

        bool setDisplayedSlicePlaneFromFace(size_t faceId, const Model& model) {

            if (!displayedState) { return false; }
            if (faceId >= model.faceCount()) { return false; }

            Pos3 normal = model.faceNormal(faceId);

            if (normal.pythag() <= 1e-6f) { return false; }

            ToolPath& toolPath = displayedState->toolPath;

            if (toolPath.hasSliceFace() && toolPath.sliceFaceId == faceId) {
                toolPath.clearSlicePlane();
            }
            else {
                toolPath.setSlicePlane(faceId, normal);
            }

            if (displayedState->hasDelta) {
                displayedState->computeToolPath(toolLibrary, selectedToolName);
            }

            dirty = true;

            return true;
        }
    };
}