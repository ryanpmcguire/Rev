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
import Cam.App.Stage;
import Cam.App.Operation;
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

        // Stages
        //--------------------------------------------------

        std::vector<Stage*> stages;

        Stage* rootStage = nullptr;
        Stage* latestCommittedStage = nullptr;
        Stage* workingStage = nullptr;
        Stage* displayedStage = nullptr;
        std::vector<Stage*> viewSelection;

        // Raw stock definition (and its auto-generated stages).
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

            for (Stage* stage : stages) {
                delete stage;
            }

            stages.clear();

            rootStage = nullptr;
            latestCommittedStage = nullptr;
            workingStage = nullptr;
            displayedStage = nullptr;
            viewSelection.clear();

            stock.reset();

            loaded = false;
        }

        bool empty() const {
            return stages.empty() || !rootStage;
        }

        bool hasFile() const {
            return file.valid && !file.pathname.empty();
        }

        bool hasProjectFile() const {
            return projectFile.valid && !projectFile.pathname.empty();
        }

        bool hasModel() const {
            return loaded && displayedStage;
        }

        void markDirty() {
            dirty = true;
        }

        // Index helpers
        //--------------------------------------------------

        size_t indexOf(Stage* stage) const {

            for (size_t i = 0; i < stages.size(); i++) {
                if (stages[i] == stage) {
                    return i;
                }
            }

            return static_cast<size_t>(-1);
        }

        Stage* stageAt(size_t index) const {

            if (index >= stages.size()) { return nullptr; }

            return stages[index];
        }

        // Rebuild parent/child links from the flat stages list (index order is history).
        void relinkStageHierarchy() {

            for (Stage* node : stages) {

                if (!node) { continue; }

                node->parent = nullptr;
                node->children.clear();
            }

            if (stages.empty()) {

                rootStage = nullptr;
                latestCommittedStage = nullptr;
                workingStage = nullptr;

                return;
            }

            rootStage = stages.front();

            for (size_t i = 1; i < stages.size(); i++) {

                Stage* prior = stages[i - 1];
                Stage* node = stages[i];

                if (!prior || !node) { continue; }

                prior->addChild(node);
            }

            latestCommittedStage = rootStage;
            workingStage = nullptr;

            for (Stage* node : stages) {

                if (!node) { continue; }

                if (node->working) {
                    workingStage = node;
                }

                if (node->committed && !node->working) {
                    latestCommittedStage = node;
                }
            }

            if (!latestCommittedStage) {
                latestCommittedStage = rootStage;
            }
        }

        void sortViewSelection() {

            std::sort(
                viewSelection.begin(),
                viewSelection.end(),
                [this](Stage* a, Stage* b) {
                    return indexOf(a) < indexOf(b);
                }
            );
        }

        Stage* primaryViewStage() const {

            Stage* primary = nullptr;

            for (Stage* stage : viewSelection) {

                if (!stage) { continue; }

                if (!primary || indexOf(stage) < indexOf(primary)) {
                    primary = stage;
                }
            }

            if (primary) { return primary; }

            return displayedStage;
        }

        bool isViewSelected(Stage* stage) const {

            if (!stage) { return false; }

            return std::find(
                viewSelection.begin(),
                viewSelection.end(),
                stage
            ) != viewSelection.end();
        }

        void syncViewSelectionToDisplayed() {

            viewSelection.clear();

            if (displayedStage) {
                viewSelection.push_back(displayedStage);
            }
        }

        void pruneViewSelection() {

            viewSelection.erase(
                std::remove_if(
                    viewSelection.begin(),
                    viewSelection.end(),
                    [this](Stage* candidate) {
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
            json["version"] = 2;

            json["name"] = name;
            json["loaded"] = loaded;

            json["sourceFile"] = {
                { "pathname", file.pathname },
                { "name", file.name },
                { "ext", file.ext }
            };

            json["toolFolderPath"] = toolFolderPath;

            json["stages"] = Json::array();

            for (size_t i = 0; i < stages.size(); i++) {

                Stage* stage = stages[i];

                if (!stage) { continue; }

                Json stageJson;

                stageJson["index"] = i;
                stageJson["name"] = stage->name;
                stageJson["committed"] = stage->committed;
                stageJson["working"] = stage->working;
                stageJson["stockGenerated"] = stage->stockGenerated;
                stageJson["hasDelta"] = stage->hasDelta;

                stageJson["operation"] = stage->operation
                    ? stage->operation->getState()
                    : ImportOperation().getState();

                size_t parentIndex = indexOf(stage->parent);

                if (parentIndex != static_cast<size_t>(-1)) { stageJson["parent"] = parentIndex; }
                else { stageJson["parent"] = nullptr; }

                stageJson["model"] = stage->model.getState();

                if (stage->hasDelta) { stageJson["delta"] = stage->delta.getState(); }
                else { stageJson["delta"] = ""; }

                // Axis frame (stock center + orientation).
                // Saved per-stage so every setup can carry its own frame,
                // and propagateAxisToAllStages keeps them in sync at runtime.
                stageJson["axisOrigin"] = Json::array({
                    stage->model.axisOrigin.x,
                    stage->model.axisOrigin.y,
                    stage->model.axisOrigin.z
                });
                stageJson["hasAxisOrigin"] = stage->model.hasAxisOrigin;

                stageJson["axisXDirection"] = Json::array({
                    stage->model.axisXDirection.x,
                    stage->model.axisXDirection.y,
                    stage->model.axisXDirection.z
                });
                stageJson["hasAxisX"] = stage->model.hasAxisX;

                stageJson["axisYDirection"] = Json::array({
                    stage->model.axisYDirection.x,
                    stage->model.axisYDirection.y,
                    stage->model.axisYDirection.z
                });
                stageJson["hasAxisY"] = stage->model.hasAxisY;

                stageJson["axisZDirection"] = Json::array({
                    stage->model.axisZDirection.x,
                    stage->model.axisZDirection.y,
                    stage->model.axisZDirection.z
                });
                stageJson["hasAxisZ"] = stage->model.hasAxisZ;

                stageJson["toolPath"] = {
                    { "toolName", stage->toolPath.toolName },
                    { "strategy", stage->toolPath.strategy },
                    { "strategyAuto", stage->toolPath.strategyAuto },
                    { "stepDown", stage->toolPath.stepDown },
                    { "stepover", stage->toolPath.stepover },
                    { "feedRate", stage->toolPath.feedRate },
                    { "rapidSpeedMmPerSec", stage->toolPath.rapidSpeedMmPerSec },
                    { "climbMilling", stage->toolPath.climbMilling },
                    { "linkRetractDistance", stage->toolPath.linkRetractDistance },
                    { "sliceAxis", Json::array({
                        stage->toolPath.sliceAxis.x,
                        stage->toolPath.sliceAxis.y,
                        stage->toolPath.sliceAxis.z
                    }) },
                    { "sliceOrigin", Json::array({
                        stage->toolPath.sliceOrigin.x,
                        stage->toolPath.sliceOrigin.y,
                        stage->toolPath.sliceOrigin.z
                    }) },
                    { "sliceFaceId", stage->toolPath.hasSliceFace()
                        ? Json(stage->toolPath.sliceFaceId)
                        : Json(nullptr)
                    },
                    { "hasToolPath", stage->hasToolPath }
                };

                json["stages"].push_back(stageJson);
            }

            json["rootStage"] = indexOf(rootStage);
            json["latestCommittedStage"] = indexOf(latestCommittedStage);
            json["workingStage"] = indexOf(workingStage);
            json["displayedStage"] = indexOf(displayedStage);

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

            std::vector<Stage*> newStages;

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

                if (!json.contains("stages") || !json["stages"].is_array()) {
                    return false;
                }

                const Json& stagesJson = json["stages"];

                newStages.resize(stagesJson.size(), nullptr);

                // First pass: create stages and hydrate model/delta data.
                for (const Json& stageJson : stagesJson) {

                    if (!stageJson.contains("index")) {
                        continue;
                    }

                    size_t index = stageJson["index"].get<size_t>();

                    if (index >= newStages.size()) {
                        continue;
                    }

                    Stage* stage = new Stage();

                    if (stageJson.contains("operation")) {
                        stage->operation = Operation::fromState(stageJson["operation"]);
                    }
                    else {
                        stage->operation = new ImportOperation();
                    }

                    if (stageJson.contains("name") && stageJson["name"].is_string()) {
                        stage->name = stageJson["name"].get<std::string>();
                    }

                    if (stageJson.contains("committed")) {
                        stage->committed = stageJson["committed"].get<bool>();
                    }

                    if (stageJson.contains("working")) {
                        stage->working = stageJson["working"].get<bool>();
                    }

                    if (stageJson.contains("stockGenerated")) {
                        stage->stockGenerated = stageJson["stockGenerated"].get<bool>();
                    }

                    if (stageJson.contains("hasDelta")) {
                        stage->hasDelta = stageJson["hasDelta"].get<bool>();
                    }

                    if (stageJson.contains("model") && stageJson["model"].is_string()) {
                        stage->model.setState(stageJson["model"].get<std::string>());
                    }

                    if (stage->hasDelta && stageJson.contains("delta") && stageJson["delta"].is_string()) {
                        stage->delta.setState(stageJson["delta"].get<std::string>());
                    }

                    // Restore axis frame.
                    auto readVec3 = [&](const char* key, Rev::Core::Pos3& out) {
                        if (
                            stageJson.contains(key) &&
                            stageJson[key].is_array() &&
                            stageJson[key].size() >= 3
                        ) {
                            out.x = stageJson[key][0].get<float>();
                            out.y = stageJson[key][1].get<float>();
                            out.z = stageJson[key][2].get<float>();
                        }
                    };

                    readVec3("axisOrigin",     stage->model.axisOrigin);
                    readVec3("axisXDirection", stage->model.axisXDirection);
                    readVec3("axisYDirection", stage->model.axisYDirection);
                    readVec3("axisZDirection", stage->model.axisZDirection);

                    if (stageJson.contains("hasAxisOrigin") && stageJson["hasAxisOrigin"].is_boolean()) {
                        stage->model.hasAxisOrigin = stageJson["hasAxisOrigin"].get<bool>();
                    }
                    if (stageJson.contains("hasAxisX") && stageJson["hasAxisX"].is_boolean()) {
                        stage->model.hasAxisX = stageJson["hasAxisX"].get<bool>();
                    }
                    if (stageJson.contains("hasAxisY") && stageJson["hasAxisY"].is_boolean()) {
                        stage->model.hasAxisY = stageJson["hasAxisY"].get<bool>();
                    }
                    if (stageJson.contains("hasAxisZ") && stageJson["hasAxisZ"].is_boolean()) {
                        stage->model.hasAxisZ = stageJson["hasAxisZ"].get<bool>();
                    }

                    if (stageJson.contains("toolPath") && stageJson["toolPath"].is_object()) {
                        const Json& toolPathJson = stageJson["toolPath"];

                        if (toolPathJson.contains("toolName") && toolPathJson["toolName"].is_string()) {
                            stage->toolPath.toolName = toolPathJson["toolName"].get<std::string>();
                        }

                        if (toolPathJson.contains("hasToolPath") && toolPathJson["hasToolPath"].is_boolean()) {
                            stage->hasToolPath = toolPathJson["hasToolPath"].get<bool>();
                        }

                        if (toolPathJson.contains("strategy") && toolPathJson["strategy"].is_string()) {
                            stage->toolPath.strategy = toolPathJson["strategy"].get<std::string>();
                        }

                        if (toolPathJson.contains("strategyAuto") && toolPathJson["strategyAuto"].is_boolean()) {
                            stage->toolPath.strategyAuto = toolPathJson["strategyAuto"].get<bool>();
                        }
                        else if (toolPathJson.contains("strategy")) {
                            // A legacy explicit strategy field means the user
                            // committed to that strategy. Lock it in so we don't
                            // override it via auto-detection.
                            stage->toolPath.strategyAuto = false;
                        }

                        if (toolPathJson.contains("stepDown") && toolPathJson["stepDown"].is_number()) {
                            stage->toolPath.stepDown = toolPathJson["stepDown"].get<double>();
                        }

                        if (toolPathJson.contains("feedRate") && toolPathJson["feedRate"].is_number()) {
                            stage->toolPath.feedRate = toolPathJson["feedRate"].get<double>();
                        }

                        if (toolPathJson.contains("stepover") && toolPathJson["stepover"].is_number()) {
                            stage->toolPath.stepover = toolPathJson["stepover"].get<double>();
                        }

                        if (toolPathJson.contains("rapidSpeedMmPerSec") && toolPathJson["rapidSpeedMmPerSec"].is_number()) {
                            stage->toolPath.rapidSpeedMmPerSec = toolPathJson["rapidSpeedMmPerSec"].get<double>();
                        }

                        if (toolPathJson.contains("climbMilling") && toolPathJson["climbMilling"].is_boolean()) {
                            stage->toolPath.climbMilling = toolPathJson["climbMilling"].get<bool>();
                        }

                        if (toolPathJson.contains("linkRetractDistance") && toolPathJson["linkRetractDistance"].is_number()) {
                            stage->toolPath.linkRetractDistance = static_cast<float>(
                                toolPathJson["linkRetractDistance"].get<double>()
                            );
                        }

                        if (toolPathJson.contains("sliceAxis") && toolPathJson["sliceAxis"].is_array() && toolPathJson["sliceAxis"].size() >= 3) {
                            stage->toolPath.sliceAxis.x = toolPathJson["sliceAxis"][0].get<float>();
                            stage->toolPath.sliceAxis.y = toolPathJson["sliceAxis"][1].get<float>();
                            stage->toolPath.sliceAxis.z = toolPathJson["sliceAxis"][2].get<float>();
                        }

                        if (toolPathJson.contains("sliceOrigin") && toolPathJson["sliceOrigin"].is_array() && toolPathJson["sliceOrigin"].size() >= 3) {
                            stage->toolPath.sliceOrigin.x = toolPathJson["sliceOrigin"][0].get<float>();
                            stage->toolPath.sliceOrigin.y = toolPathJson["sliceOrigin"][1].get<float>();
                            stage->toolPath.sliceOrigin.z = toolPathJson["sliceOrigin"][2].get<float>();
                        }

                        if (toolPathJson.contains("sliceFaceId") && !toolPathJson["sliceFaceId"].is_null()) {
                            stage->toolPath.sliceFaceId = toolPathJson["sliceFaceId"].get<size_t>();
                        }
                        else {
                            stage->toolPath.sliceFaceId = ToolPath::NoSliceFaceId;
                        }
                    }

                    newStages[index] = stage;
                }

                // Second pass: restore parent links.
                for (const Json& stageJson : stagesJson) {

                    if (!stageJson.contains("index")) {
                        continue;
                    }

                    size_t index = stageJson["index"].get<size_t>();

                    if (index >= newStages.size()) {
                        continue;
                    }

                    Stage* stage = newStages[index];

                    if (!stage) { continue; }

                    if (stageJson.contains("parent") && stageJson["parent"].is_number_unsigned()) {
                        size_t parentIndex = stageJson["parent"].get<size_t>();

                        if (parentIndex < newStages.size()) {
                            stage->parent = newStages[parentIndex];
                        }
                    }
                }

                Stage* newRoot = nullptr;
                Stage* newLatestCommitted = nullptr;
                Stage* newWorking = nullptr;
                Stage* newDisplayed = nullptr;

                if (json.contains("rootStage")) {
                    newRoot = stageAtJsonIndex(newStages, json["rootStage"]);
                }

                if (json.contains("latestCommittedStage")) {
                    newLatestCommitted = stageAtJsonIndex(newStages, json["latestCommittedStage"]);
                }

                if (json.contains("workingStage")) {
                    newWorking = stageAtJsonIndex(newStages, json["workingStage"]);
                }

                if (json.contains("displayedStage")) {
                    newDisplayed = stageAtJsonIndex(newStages, json["displayedStage"]);
                }

                std::vector<Stage*> oldStages = stages;

                stages = newStages;

                rootStage = newRoot;
                latestCommittedStage = newLatestCommitted;
                workingStage = newWorking;
                displayedStage = newDisplayed;

                if (!rootStage && !stages.empty()) {
                    rootStage = stages.front();
                }

                if (!latestCommittedStage) {
                    latestCommittedStage = rootStage;
                }

                if (!displayedStage) {
                    displayedStage = workingStage ? workingStage : latestCommittedStage;
                }

                relinkStageHierarchy();

                syncViewSelectionToDisplayed();

                loaded = rootStage != nullptr;
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

                ensureToolPathComputed(displayedStage);

                for (Stage* oldStage : oldStages) {
                    delete oldStage;
                }

                return true;
            }

            catch (...) {

                for (Stage* stage : newStages) {
                    delete stage;
                }

                return false;
            }
        }

        static Stage* stageAtJsonIndex(const std::vector<Stage*>& list, const Json& indexJson) {
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

            Stage* newRoot = Stage::FromImport(selected);

            if (!newRoot) { return false; }

            Stage* newWorking = Stage::FromPrior(newRoot);

            std::vector<Stage*> oldStages = stages;

            stages.clear();

            file = selected;

            if (!file.name.empty()) {
                name = file.name;
            }

            rootStage = newRoot;
            latestCommittedStage = newRoot;
            workingStage = newWorking;
            displayedStage = newWorking ? newWorking : newRoot;
            syncViewSelectionToDisplayed();

            stages.push_back(rootStage);

            if (workingStage) {
                stages.push_back(workingStage);
            }

            relinkStageHierarchy();

            loaded = true;
            dirty = true;

            for (Stage* stage : oldStages) {
                delete stage;
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

            if (!displayedStage) { return nullptr; }

            return &displayedStage->model;
        }

        void ensureToolPathComputed(Stage* stage) {

            if (!stage || !stage->needsToolPathComputation()) { return; }

            stage->computeToolPath(toolLibrary, selectedToolName);
        }

        // Connect each stage's toolpath to the previous one in history (lower index).
        // prior = stages[i] (earlier operation), next = stages[i-1] (later operation).
        // Adds a rapid link move from prior's retract point to next's approach point
        // so the preview plays as one continuous motion with correct timing.
        void linkStageToolPaths() {

            for (Stage* stage : stages) {
                ensureToolPathComputed(stage);
            }

            for (size_t i = 1; i < stages.size(); i++) {

                Stage* prior = stages[i];
                Stage* next  = stages[i - 1];

                if (!prior || !next) { continue; }

                prior->link(next);
            }
        }

        bool selectStage(Stage* stage, bool addToSelection = false) {

            if (!stage) { return false; }

            if (!addToSelection) {
                viewSelection = { stage };
            }
            else if (!isViewSelected(stage)) {
                viewSelection.push_back(stage);
                sortViewSelection();
            }

            displayedStage = stage;

            for (Stage* selected : viewSelection) {
                ensureToolPathComputed(selected);
            }

            return true;
        }

        // Stages are stored with index 0 = most recent; higher index = earlier in time.
        // Forward preview advances toward lower indices.
        Stage* nextStageAfterViewSelection() const {

            if (stages.empty()) { return nullptr; }

            size_t anchorIndex = static_cast<size_t>(-1);

            for (Stage* stage : viewSelection) {

                if (!stage) { continue; }

                const size_t index = indexOf(stage);

                if (index == static_cast<size_t>(-1)) { continue; }

                if (
                    anchorIndex == static_cast<size_t>(-1) ||
                    index < anchorIndex
                ) {
                    anchorIndex = index;
                }
            }

            if (anchorIndex == static_cast<size_t>(-1)) {

                if (!displayedStage) { return nullptr; }

                anchorIndex = indexOf(displayedStage);

                if (anchorIndex == static_cast<size_t>(-1)) { return nullptr; }
            }

            if (anchorIndex == 0) { return nullptr; }

            return stages[anchorIndex - 1];
        }

        // Earlier stage before the highest index in viewSelection (forward-time back).
        Stage* previousStageBeforeViewSelection() const {

            if (stages.empty()) { return nullptr; }

            size_t anchorIndex = static_cast<size_t>(-1);

            for (Stage* stage : viewSelection) {

                if (!stage) { continue; }

                const size_t index = indexOf(stage);

                if (index == static_cast<size_t>(-1)) { continue; }

                if (
                    anchorIndex == static_cast<size_t>(-1) ||
                    index > anchorIndex
                ) {
                    anchorIndex = index;
                }
            }

            if (anchorIndex == static_cast<size_t>(-1)) {

                if (!displayedStage) { return nullptr; }

                anchorIndex = indexOf(displayedStage);

                if (anchorIndex == static_cast<size_t>(-1)) { return nullptr; }
            }

            if (anchorIndex + 1 >= stages.size()) { return nullptr; }

            return stages[anchorIndex + 1];
        }

        // Stage editing
        //--------------------------------------------------

        bool deleteStage(Stage* stage) {

            if (!stage) { return false; }

            size_t index = indexOf(stage);

            if (index == static_cast<size_t>(-1)) { return false; }
            if (index == 0) { return false; }

            Stage* anchor = stages[index - 1];

            std::vector<Stage*> toDelete(
                stages.begin() + static_cast<std::ptrdiff_t>(index),
                stages.end()
            );

            // Deleting any generated stock stage tears down the whole stock,
            // so reset the definition and re-offer the "Generate Stock" button.
            bool removedStock = false;

            for (Stage* node : toDelete) {
                if (node && node->stockGenerated) { removedStock = true; break; }
            }

            if (removedStock) {
                stock.defined = false;
                stock.initialized = false;
            }

            auto isRemoved = [&toDelete](Stage* candidate) {

                if (!candidate) { return true; }

                return std::find(toDelete.begin(), toDelete.end(), candidate) != toDelete.end();
            };

            if (displayedStage && isRemoved(displayedStage)) {
                displayedStage = anchor;
            }

            if (workingStage && isRemoved(workingStage)) {
                workingStage = nullptr;
            }

            if (latestCommittedStage && isRemoved(latestCommittedStage)) {
                latestCommittedStage = anchor;
            }

            viewSelection.erase(
                std::remove_if(
                    viewSelection.begin(),
                    viewSelection.end(),
                    [&](Stage* candidate) { return isRemoved(candidate); }
                ),
                viewSelection.end()
            );

            stages.erase(
                stages.begin() + static_cast<std::ptrdiff_t>(index),
                stages.end()
            );

            for (Stage* node : toDelete) {

                node->parent = nullptr;
                node->children.clear();
                node->model.clear();
                node->clearDelta();

                delete node->operation;
                node->operation = nullptr;

                delete node;
            }

            relinkStageHierarchy();

            if (!workingStage && latestCommittedStage) {

                workingStage = Stage::FromPrior(latestCommittedStage);

                if (workingStage) {
                    stages.push_back(workingStage);
                    relinkStageHierarchy();
                }
            }

            if (!displayedStage) {
                displayedStage = workingStage ? workingStage : latestCommittedStage;
            }

            pruneViewSelection();

            if (workingStage) {
                displayedStage = workingStage;
            }
            else if (!displayedStage) {
                displayedStage = latestCommittedStage;
            }

            syncViewSelectionToDisplayed();

            loaded = rootStage != nullptr;
            dirty = true;

            return true;
        }

        // Stock definition
        //--------------------------------------------------

        // The deepest user (non-stock) stage — the rectangular prism the stock
        // grows out from.
        Stage* stockBaseStage() const {

            if (workingStage && !workingStage->stockGenerated) {
                return workingStage;
            }

            for (auto it = stages.rbegin(); it != stages.rend(); ++it) {
                if (*it && !(*it)->stockGenerated) { return *it; }
            }

            return nullptr;
        }

        // Stock can be defined once the base part is literally a box.
        bool stockMenuAvailable() {

            Stage* base = stockBaseStage();

            return base && base->model.isRectangularPrism();
        }

        // Axis-frame extents (X along axis, Y/Z cross-section) of the base part.
        bool partFrameExtents(double& extX, double& extY, double& extZ) {

            Stage* base = stockBaseStage();

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

            for (Stage* s : stages) {
                if (s && s->stockGenerated) { return true; }
            }

            return false;
        }

        // Detach and delete an (empty) working-copy stage, splicing it out of the
        // history without spawning a replacement working stage.
        void removeWorkingCopyStage(Stage* s) {

            if (!s) { return; }

            if (s->parent) {
                auto& children = s->parent->children;
                children.erase(
                    std::remove(children.begin(), children.end(), s),
                    children.end()
                );
            }

            stages.erase(std::remove(stages.begin(), stages.end(), s), stages.end());

            if (displayedStage == s)        { displayedStage = s->parent; }
            if (latestCommittedStage == s)  { latestCommittedStage = s->parent; }

            viewSelection.erase(
                std::remove(viewSelection.begin(), viewSelection.end(), s),
                viewSelection.end()
            );

            s->parent = nullptr;
            s->children.clear();
            s->model.clear();
            s->clearDelta();

            delete s->operation;
            s->operation = nullptr;

            delete s;

            if (!displayedStage) {
                displayedStage = latestCommittedStage ? latestCommittedStage : rootStage;
            }

            if (viewSelection.empty()) {
                syncViewSelectionToDisplayed();
            }
        }

        // One-time stock generation, driven by the "Generate Stock" button.
        // Consumes the current working stage (so no vestigial editable copy is
        // left parenting the stock chain), fills default dimensions from the
        // part bounding box, and builds the four stock stages.
        bool generateStock() {

            if (stock.defined) { return false; }
            if (!stockMenuAvailable()) { return false; }

            Stage* base = stockBaseStage();

            if (!base) { return false; }

            // Make sure the prism is a permanent, non-editable boundary rather
            // than a trailing working stage that would orphan the stock chain.
            if (base == workingStage) {

                const bool emptyCopy =
                    base->parent &&
                    !base->hasDelta &&
                    !base->model.changed;

                if (emptyCopy) {
                    // The part was already a prism; drop the redundant copy and
                    // build stock off the committed parent prism.
                    Stage* parent = base->parent;
                    removeWorkingCopyStage(base);
                    base = parent;
                }
                else {
                    // A real operation produced the prism; finalize it in place.
                    base->working = false;
                    base->committed = true;
                    latestCommittedStage = base;
                }

                workingStage = nullptr;
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

            if (displayedStage == nullptr) {
                displayedStage = base;
                syncViewSelectionToDisplayed();
            }

            regenerateStockStages();

            return true;
        }

        void copyAxisFrame(const Model& src, Model& dst) {

            dst.axisOrigin    = src.axisOrigin;    dst.hasAxisOrigin = src.hasAxisOrigin;
            dst.axisXDirection = src.axisXDirection; dst.hasAxisX = src.hasAxisX;
            dst.axisYDirection = src.axisYDirection; dst.hasAxisY = src.hasAxisY;
            dst.axisZDirection = src.axisZDirection; dst.hasAxisZ = src.hasAxisZ;
        }

        // Remove and delete all auto-generated stock stages, restoring the
        // user chain to its pristine pre-stock form.
        void removeStockStages() {

            std::vector<Stage*> stockStages;
            std::vector<Stage*> kept;

            for (Stage* s : stages) {
                if (s && s->stockGenerated) { stockStages.push_back(s); }
                else { kept.push_back(s); }
            }

            if (stockStages.empty()) { return; }

            stages = kept;

            // Drop child links from kept stages into the (deleted) stock chain.
            for (Stage* s : kept) {

                if (!s) { continue; }

                s->children.erase(
                    std::remove_if(
                        s->children.begin(),
                        s->children.end(),
                        [](Stage* c) { return c && c->stockGenerated; }
                    ),
                    s->children.end()
                );
            }

            for (Stage* s : stockStages) {

                if (displayedStage == s) { displayedStage = nullptr; }

                s->parent = nullptr;
                s->children.clear();
                s->model.clear();
                s->clearDelta();

                delete s->operation;
                s->operation = nullptr;

                delete s;
            }

            viewSelection.erase(
                std::remove_if(
                    viewSelection.begin(),
                    viewSelection.end(),
                    [&](Stage* c) {
                        return std::find(kept.begin(), kept.end(), c) == kept.end();
                    }
                ),
                viewSelection.end()
            );

            if (!displayedStage) {
                displayedStage = workingStage ? workingStage : latestCommittedStage;
            }

            if (viewSelection.empty()) {
                syncViewSelectionToDisplayed();
            }
        }

        // Rebuild the four stock stages from the current stock definition.
        void regenerateStockStages() {

            removeStockStages();

            if (!stock.defined) {
                dirty = true;
                return;
            }

            Stage* base = stockBaseStage();

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

            // Slice axis for each generated stage = the outward normal of the
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

            Stage* parent = base;

            for (size_t i = 0; i < shapes.size(); i++) {

                Stage* s = new Stage();

                s->operation = new ExtendFeatureOperation();

                s->model = Model::FromShape(shapes[i]);

                if (!s->model.loaded) {
                    dbg("[Stock] step %zu produced an unusable solid; skipping", i);
                    delete s->operation;
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

                stages.push_back(s);

                s->computeDelta(toolLibrary, selectedToolName);

                parent = s;
            }

            dirty = true;
        }

        // Operations
        //--------------------------------------------------

        // Apply an operation to the working stage. Takes ownership of `op`.
        bool applyOperation(Operation* op) {

            if (!op) { return false; }

            if (!workingStage) {
                dbg("[Operation] failed: no working stage");
                delete op;
                return false;
            }

            displayedStage = workingStage;
            syncViewSelectionToDisplayed();

            delete workingStage->operation;
            workingStage->operation = op;

            bool ok = op->apply(workingStage->model);

            if (!ok) { return false; }

            workingStage->model.clearSelection();
            workingStage->computeDelta(toolLibrary, selectedToolName);

            dirty = true;

            return true;
        }

        bool defeatureSelected() {
            return applyOperation(new DefeatureOperation());
        }

        bool extendSelected(double distance = 10.0) {
            ExtendFeatureOperation* op = new ExtendFeatureOperation();
            op->distance = distance;
            return applyOperation(op);
        }

        bool commitWorkingStage() {

            if (!workingStage) { return false; }
            if (!workingStage->model.changed && !workingStage->hasDelta) { return false; }

            workingStage->computeDelta(toolLibrary, selectedToolName);

            workingStage->committed = true;
            workingStage->working = false;
            workingStage->name = "Material State";

            latestCommittedStage = workingStage;

            workingStage = Stage::FromPrior(latestCommittedStage);

            if (workingStage) {
                stages.push_back(workingStage);
            }

            relinkStageHierarchy();

            displayedStage = workingStage ? workingStage : latestCommittedStage;
            syncViewSelectionToDisplayed();

            loaded = rootStage != nullptr;
            dirty = true;

            return true;
        }

        bool recalculateDisplayedToolPath() {

            if (!displayedStage) { return false; }
            if (!displayedStage->parent) { return false; }

            displayedStage->computeDelta(toolLibrary, selectedToolName);

            dirty = true;

            return displayedStage->hasToolPath;
        }

        bool setDisplayedSlicePlaneFromFace(size_t faceId, const Model& model) {

            if (!displayedStage) { return false; }
            if (faceId >= model.faceCount()) { return false; }

            Pos3 normal = model.faceNormal(faceId);

            if (normal.pythag() <= 1e-6f) { return false; }

            ToolPath& toolPath = displayedStage->toolPath;

            if (toolPath.hasSliceFace() && toolPath.sliceFaceId == faceId) {
                toolPath.clearSlicePlane();
            }
            else {
                toolPath.setSlicePlane(faceId, normal);
            }

            if (displayedStage->hasDelta) {
                displayedStage->computeToolPath(toolLibrary, selectedToolName);
            }

            dirty = true;

            return true;
        }
    };
}
