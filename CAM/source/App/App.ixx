module;

#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>

#include <managed.hpp>

#include <dbg.hpp>

export module Cam.App;

import Rev.OS.File;
import Rev.OS.Dialog;

import Cam.App.Project;
import Cam.App.Persist;
import Cam.App.MachineSettings;
import Cam.App.MaterialState;
import Cam.App.Model;
import Cam.App.Tool;
import Cam.App.ToolLibrary;
import Cam.App.MachineProfile;
import Cam.App.MachineLibrary;

export namespace Cam::App {

    struct AppState {

        // Projects
        std::vector<Project*> projects;
        Project* activeProject = nullptr;

        // Cached tool folder path for the active project (or app default).
        std::string toolFolderPath = "";

        // App-wide machine library (definitions + STEP asset references).
        MachineLibrary machineLibrary;
        std::string machinesRootPath = "";
        std::vector<std::string> knownMachineFolderPaths;
        std::string selectedMachineName = "";

        // App-wide machine settings (origins, etc.), persisted with the session.
        MachineSettings machine;

        // Create
        //--------------------------------------------------

        static AppState* Get(void*& state) {

            AppState* app = static_cast<AppState*>(state);

            if (!app) { app = new AppState(); state = app; }

            return app;
        }

        AppState() {
            loadSessionOrDefaults();
            loadTools();
            loadMachines();
        }

        // Destroy
        //--------------------------------------------------

        ~AppState() {

            for (Project* project : projects) {
                delete project;
            }

            projects.clear();
            activeProject = nullptr;
        }

        // Projects
        //--------------------------------------------------

        void loadSessionOrDefaults() {

            projects.clear();
            activeProject = nullptr;

            Project* loadedActive = nullptr;

            if (Persist::load(
                projects,
                loadedActive,
                machine,
                machinesRootPath,
                knownMachineFolderPaths,
                selectedMachineName
            )) {
                activeProject = loadedActive;
                return;
            }

            // No persist (or empty session): start with no projects.
            projects.clear();
            activeProject = nullptr;
        }

        bool saveSession() {
            return Persist::save(
                projects,
                activeProject,
                machine,
                machinesRootPath,
                knownMachineFolderPaths,
                selectedMachineName
            );
        }

        // Tools
        //--------------------------------------------------

        ToolLibrary* toolLibrary() {

            if (!activeProject) { return nullptr; }

            return &activeProject->toolLibrary;
        }

        const ToolLibrary* toolLibrary() const {

            if (!activeProject) { return nullptr; }

            return &activeProject->toolLibrary;
        }

        void loadTools() {

            if (!activeProject) {
                toolFolderPath = ToolLibrary::defaultToolFolderPath();
                return;
            }

            activeProject->loadToolLibrary();

            toolFolderPath = activeProject->toolFolderPath;
        }

        size_t toolCount() const {

            const ToolLibrary* library = toolLibrary();

            if (!library) { return 0; }

            return library->size();
        }

        Tool* toolAt(size_t index) {

            ToolLibrary* library = toolLibrary();

            if (!library) { return nullptr; }

            return library->at(index);
        }

        Tool* selectedTool() {

            if (!activeProject) { return nullptr; }

            return activeProject->toolLibrary.find(activeProject->selectedToolName);
        }

        bool selectTool(size_t index) {

            if (!activeProject) { return false; }

            Tool* tool = activeProject->toolLibrary.at(index);

            if (!tool) { return false; }

            activeProject->selectedToolName = tool->name;

            return true;
        }

        bool selectToolByName(const std::string& name) {

            if (!activeProject) { return false; }

            if (!activeProject->toolLibrary.find(name)) {
                return false;
            }

            activeProject->selectedToolName = name;

            return true;
        }

        bool createNewTool(std::string& outName) {

            if (!activeProject) { return false; }

            ToolLibrary& library = activeProject->toolLibrary;

            Tool tool;
            tool.name = ToolLibrary::uniqueToolName(library);
            tool.type = Tool::Type::EndMill;
            tool.diameter = 1.0;
            tool.radius = 0.5;
            tool.cuttingLength = 20.0;
            tool.collarLength = 80.0;
            tool.recomputeLength();

            if (!library.insertTool(tool)) { return false; }

            activeProject->selectedToolName = tool.name;
            outName = tool.name;

            return true;
        }

        bool isUnsavedTool(const std::string& name) const {

            const ToolLibrary* library = toolLibrary();

            if (!library) {
                return false;
            }

            const Tool* tool = library->find(name);

            if (!tool) { return false; }

            return tool->filePath.empty();
        }

        bool removeTool(const std::string& name) {

            if (!activeProject) { return false; }

            ToolLibrary& library = activeProject->toolLibrary;

            if (!library.removeTool(name)) {
                return false;
            }

            if (activeProject->selectedToolName == name) {

                if (!library.empty()) { activeProject->selectedToolName = library.order.front(); }
                else { activeProject->selectedToolName.clear(); }
            }

            activeProject->dirty = true;

            return true;
        }

        bool saveTool(const std::string& originalName, const Tool& src) {

            const std::string& newName = src.name;

            if (!activeProject || newName.empty()) {
                return false;
            }

            Tool* tool = activeProject->toolLibrary.find(originalName);

            if (!tool) { return false; }

            Tool updated = *tool;
            updated.name = newName;
            updated.type = src.type;
            updated.diameter = src.diameter;
            updated.radius = src.diameter * 0.5;
            updated.length = src.length;

            updated.taperAngle = src.taperAngle;
            updated.cuttingLength = src.cuttingLength;
            updated.shoulderDiameter = src.shoulderDiameter;
            updated.shoulderLength = src.shoulderLength;
            updated.shoulderTaperAngle = src.shoulderTaperAngle;
            updated.collarDiameter = src.collarDiameter;
            updated.collarLength = src.collarLength;

            // Overall length is derived from the profile.
            updated.recomputeLength();

            updated.defaultFeedRate = src.defaultFeedRate;
            updated.defaultStepdown = src.defaultStepdown;
            updated.defaultStepover = src.defaultStepover;
            updated.defaultRapidSpeed = src.defaultRapidSpeed;
            updated.defaultClimbMilling = src.defaultClimbMilling;

            std::string targetPath = tool->filePath;

            // First save: prompt for a file path.
            if (targetPath.empty()) {

                std::string initialDir = activeProject->toolFolderPath;

                if (initialDir.empty()) {
                    initialDir = ToolLibrary::defaultToolFolderPath();
                }

                std::filesystem::path suggested(initialDir);
                suggested /= ToolLibrary::toolFileName(updated);

                Rev::OS::File file({
                    .pathname = suggested.string()
                });

                if (!file.saveAs("Save Tool", "CAM Tool\0*.json\0All Files\0*.*\0", initialDir)) { return false; }

                targetPath = file.pathname;
                updated.filePath = targetPath;
            }
            // Rename: move the existing file on disk.
            else if (newName != originalName) {

                std::filesystem::path oldPath(targetPath);
                std::filesystem::path newPath =
                    oldPath.parent_path() / ToolLibrary::toolFileName(updated);

                std::error_code ec;
                std::filesystem::rename(oldPath, newPath, ec);

                if (!ec) { targetPath = newPath.string(); }
                else {
                    std::filesystem::remove(oldPath, ec);
                    targetPath = newPath.string();
                }

                updated.filePath = targetPath;
            }

            if (!activeProject->toolLibrary.replaceTool(originalName, updated)) { return false; }

            if (newName != originalName && activeProject->selectedToolName == originalName) {
                activeProject->selectedToolName = newName;
            }

            if (!ToolLibrary::saveToolFileAtPath(targetPath, updated)) { return false; }

            activeProject->dirty = true;

            return true;
        }

        bool selectToolFolder() {

            Rev::OS::File folder;

            if (!folder.selectFolder("Select Tool Folder", toolFolderPath)) { return false; }

            if (!activeProject) { return false; }

            activeProject->toolFolderPath = folder.pathname;
            activeProject->dirty = true;

            toolFolderPath = folder.pathname;

            loadTools();

            return true;
        }

        // Machines
        //--------------------------------------------------

        void rememberMachineFolder(const std::string& folderPath) {
            if (folderPath.empty()) { return; }

            if (std::find(knownMachineFolderPaths.begin(), knownMachineFolderPaths.end(), folderPath)
                == knownMachineFolderPaths.end()) {
                knownMachineFolderPaths.push_back(folderPath);
            }
        }

        std::string machineStorageFolder(const std::string& machineName) const {
            const MachineProfile* machine = machineLibrary.find(machineName);

            if (machine && !machine->filePath.empty()) {
                return std::filesystem::path(machine->filePath).parent_path().string();
            }

            std::string root = machinesRootPath;
            MachineLibrary::assignDefaultMachinesRootIfNeeded(root);

            return MachineLibrary::machineFolderPath(root, machineName);
        }

        void loadMachines() {
            MachineLibrary::assignDefaultMachinesRootIfNeeded(machinesRootPath);
            machineLibrary.loadFromMachinesRoot(machinesRootPath);

            for (const std::string& name : machineLibrary.order) {
                rememberMachineFolder(machineStorageFolder(name));
            }

            if (selectedMachineName.empty() && !machineLibrary.empty()) {
                if (machineLibrary.find("Carvera Air")) {
                    selectedMachineName = "Carvera Air";
                }
                else {
                    selectedMachineName = machineLibrary.order.front();
                }
            }

            if (!selectedMachineName.empty() && !machineLibrary.find(selectedMachineName)) {
                if (!machineLibrary.empty()) {
                    selectedMachineName = machineLibrary.order.front();
                }
                else {
                    selectedMachineName.clear();
                }
            }
        }

        size_t machineCount() const {
            return machineLibrary.size();
        }

        MachineProfile* machineAt(size_t index) {
            return machineLibrary.at(index);
        }

        MachineProfile* selectedMachine() {
            if (selectedMachineName.empty()) { return nullptr; }
            return machineLibrary.find(selectedMachineName);
        }

        bool selectMachine(size_t index) {
            MachineProfile* machine = machineLibrary.at(index);
            if (!machine) { return false; }

            selectedMachineName = machine->name;
            saveSession();
            return true;
        }

        bool selectMachineByName(const std::string& name) {
            if (!machineLibrary.find(name)) { return false; }

            selectedMachineName = name;
            saveSession();
            return true;
        }

        bool isUnsavedMachine(const std::string& name) const {
            const MachineProfile* machine = machineLibrary.find(name);
            if (!machine) { return false; }
            return machine->filePath.empty();
        }

        bool createNewMachine(std::string& outName) {
            MachineProfile machine;
            machine.name = MachineLibrary::uniqueMachineName(machineLibrary);
            machineLibrary.insertMachine(machine);
            selectedMachineName = machine.name;
            outName = machine.name;
            return true;
        }

        bool removeMachine(const std::string& name) {
            if (!machineLibrary.removeMachine(name)) { return false; }

            if (selectedMachineName == name) {
                if (!machineLibrary.empty()) {
                    selectedMachineName = machineLibrary.order.front();
                }
                else {
                    selectedMachineName.clear();
                }
            }

            saveSession();
            return true;
        }

        bool saveMachine(
            const std::string& originalName,
            const MachineProfile& src,
            const MachineStepSources& stepSources
        ) {
            const std::string& newName = src.name;

            if (newName.empty()) { return false; }

            MachineProfile* machine = machineLibrary.find(originalName);
            if (!machine) { return false; }

            MachineProfile updated = *machine;
            updated.name = newName;
            updated.spindleMinRpm = src.spindleMinRpm;
            updated.spindleMaxRpm = src.spindleMaxRpm;
            updated.axisX = src.axisX;
            updated.axisY = src.axisY;
            updated.axisZ = src.axisZ;
            updated.rotaryX = src.rotaryX;
            updated.rotaryY = src.rotaryY;
            updated.rotaryZ = src.rotaryZ;

            MachineLibrary::assignDefaultMachinesRootIfNeeded(machinesRootPath);

            if (newName != originalName) {
                MachineLibrary::renameMachineFolder(machinesRootPath, originalName, newName);
            }

            const std::string folderPath = MachineLibrary::machineFolderPath(machinesRootPath, newName);
            if (!MachineLibrary::ensureMachineFolder(folderPath)) { return false; }

            const std::string targetPath = MachineLibrary::machineJsonPath(machinesRootPath, updated);
            updated.filePath = targetPath;

            if (!machine->filePath.empty() && machine->filePath != targetPath) {
                std::error_code ec;
                std::filesystem::remove(machine->filePath, ec);
            }

            if (!MachineLibrary::applyStepSources(folderPath, updated, stepSources)) {
                dbg("[AppState] One or more STEP assets failed to copy for \"%s\"", newName.c_str());
            }

            MachineLibrary::reloadSpindleModel(updated);

            if (!machineLibrary.replaceMachine(originalName, updated)) { return false; }

            if (newName != originalName && selectedMachineName == originalName) {
                selectedMachineName = newName;
            }

            if (!MachineLibrary::saveMachineFileAtPath(targetPath, updated)) { return false; }

            rememberMachineFolder(folderPath);
            saveSession();
            return true;
        }

        bool selectMachinesRoot() {
            Rev::OS::File folder;

            if (!folder.selectFolder("Select Machines Library Folder", machinesRootPath)) { return false; }

            machinesRootPath = folder.pathname;
            MachineLibrary::normalizeMachinesRoot(machinesRootPath);
            loadMachines();
            saveSession();

            return true;
        }

        // Project lifecycle
        //--------------------------------------------------

        Project* createProject(bool loadDefault = false, std::string name = "Untitled Project") {
            Project* project = new Project(loadDefault, name);

            projects.push_back(project);

            if (!activeProject) {
                activeProject = project;
            }

            return project;
        }

        Project* createEmptyProject(std::string name = "Untitled Project") {
            return createProject(false, name);
        }

        Project* newProject() {

            Project* project = createEmptyProject("Untitled Project");

            activeProject = project;

            loadTools();
            saveSession();

            return project;
        }

        // Unsaved changes
        //--------------------------------------------------

        // Save / discard / cancel for one dirty project.
        // Returns false if the user cancelled or save failed.
        bool tryResolveDirtyProject(Project* project) {

            if (!project || !project->dirty) { return true; }

            Rev::OS::UnsavedChangesResult result =
                Rev::OS::Dialog::UnsavedChanges(project->name);

            if (result == Rev::OS::UnsavedChangesResult::Cancel) { return false; }

            if (result == Rev::OS::UnsavedChangesResult::Save) {
                return project->save();
            }

            return true;
        }

        // Prompt for each dirty project in order; cancel aborts quit.
        // On success, all projects are closed (app is shutting down).
        bool confirmApplicationClose() {

            for (Project* project : projects) {

                if (!tryResolveDirtyProject(project)) {
                    return false;
                }
            }

            saveSession();

            for (Project* project : projects) {
                delete project;
            }

            projects.clear();
            activeProject = nullptr;

            return true;
        }

        bool closeProject(Project* project) {

            if (!project) { return false; }

            auto it = std::find(projects.begin(), projects.end(), project);

            if (it == projects.end()) { return false; }

            if (!tryResolveDirtyProject(project)) { return false; }

            bool wasActive = (project == activeProject);

            size_t index = static_cast<size_t>(std::distance(projects.begin(), it));

            projects.erase(it);

            delete project;

            if (projects.empty()) {
                activeProject = nullptr;
                saveSession();
                return true;
            }

            if (wasActive) {

                if (index >= projects.size()) {
                    index = projects.size() - 1;
                }

                activeProject = projects[index];
            }

            saveSession();

            return true;
        }

        bool closeProject(size_t index) {

            if (index >= projects.size()) { return false; }

            return closeProject(projects[index]);
        }

        size_t activeProjectIndex() const {

            for (size_t i = 0; i < projects.size(); i++) {
                if (projects[i] == activeProject) { return i; }
            }

            return 0;
        }

        // Project file commands
        //--------------------------------------------------

        bool saveProject() {

            if (!activeProject) { return false; }

            if (!activeProject->save()) { return false; }

            saveSession();

            return true;
        }

        bool saveProjectAs() {

            if (!activeProject) { return false; }

            if (!activeProject->saveAs()) { return false; }

            saveSession();

            return true;
        }

        bool openProject() {

            Rev::OS::File selected;

            if (!selected.open("Open CAM Project", "CAM Project\0*.cam\0JSON Files\0*.json\0All Files\0*.*\0")) { return false; }

            Project* project = createEmptyProject("Untitled Project");

            if (!project->loadProjectFile(selected)) {

                closeProject(project);
                return false;
            }

            activeProject = project;

            loadTools();
            saveSession();

            return true;
        }

        bool setActiveProject(Project* project) {

            if (!project) { return false; }

            auto it = std::find(projects.begin(), projects.end(), project);

            if (it == projects.end()) { return false; }

            activeProject = project;

            loadTools();
            saveSession();

            return true;
        }

        bool setActiveProject(size_t index) {

            if (index >= projects.size()) { return false; }

            activeProject = projects[index];

            loadTools();
            saveSession();

            return true;
        }

        // Active project forwarding
        //--------------------------------------------------

        Model* getDisplayedModel() {

            if (!activeProject) { return nullptr; }

            return activeProject->getDisplayedModel();
        }

        bool selectState(MaterialState* state, bool addToSelection = false) {

            if (!activeProject) { return false; }

            return activeProject->selectState(state, addToSelection);
        }

        bool deleteState(MaterialState* state) {

            if (!activeProject) { return false; }

            return activeProject->deleteState(state);
        }

        bool defeatureSelected() {

            if (!activeProject) { return false; }

            return activeProject->defeatureSelected();
        }

        bool extendSelected(double distance = 10.0) {

            if (!activeProject) { return false; }

            return activeProject->extendSelected(distance);
        }

        bool commitWorkingState() {

            if (!activeProject) { return false; }

            return activeProject->commitWorkingState();
        }

        bool recalculateToolPath() {

            if (!activeProject) { return false; }

            return activeProject->recalculateDisplayedToolPath();
        }

        bool setDisplayedSlicePlaneFromFace(size_t faceId, const Model& model) {

            if (!activeProject) { return false; }

            return activeProject->setDisplayedSlicePlaneFromFace(faceId, model);
        }

        bool saveToolPathSettings(
            MaterialState* state,
            const std::string& strategy,
            const std::string& toolName,
            double stepDown,
            double stepover,
            double feedRate,
            double rapidSpeedMmPerSec,
            bool climbMilling,
            float linkRetractDistance
        ) {

            if (!activeProject || !state || toolName.empty() || stepover <= 0.0) {
                return false;
            }

            auto it = std::find(activeProject->states.begin(), activeProject->states.end(), state);

            if (it == activeProject->states.end()) { return false; }

            state->toolPath.strategy = strategy;
            state->toolPath.strategyAuto = false;
            state->toolPath.toolName = toolName;
            state->toolPath.stepDown = stepDown;
            state->toolPath.stepover = stepover;
            state->toolPath.feedRate = feedRate;
            state->toolPath.rapidSpeedMmPerSec = rapidSpeedMmPerSec;
            state->toolPath.climbMilling = climbMilling;
            state->toolPath.linkRetractDistance = linkRetractDistance;

            if (state->hasDelta) {
                state->computeToolPath(activeProject->toolLibrary, activeProject->selectedToolName);
            }

            activeProject->dirty = true;

            return true;
        }
    };
}