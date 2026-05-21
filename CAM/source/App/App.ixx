module;

#include <string>
#include <vector>
#include <algorithm>

#include <managed.hpp>

export module Cam.App;

import Rev.OS.File;
import Rev.OS.Dialog;

import Cam.App.Project;
import Cam.App.Persist;
import Cam.App.MaterialState;
import Cam.App.Model;
import Cam.App.Tool;

export namespace Cam::App {

    struct AppState {

        std::vector<Project*> projects;
        Project* activeProject = nullptr;

        std::vector<Tool> tools;
        size_t selectedToolIndex = 0;
        std::string toolFolderPath = "";

        static AppState* Get(void*& state) {

            AppState* app = static_cast<AppState*>(state);

            if (!app) {
                app = new AppState();
                state = app;
            }

            return app;
        }

        AppState() {
            loadSessionOrDefaults();
            initDefaultTools();
        }

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

            if (Persist::load(projects, loadedActive)) {
                activeProject = loadedActive;
                return;
            }

            // No persist (or empty session): start with no projects.
            projects.clear();
            activeProject = nullptr;
        }

        bool saveSession() {
            return Persist::save(projects, activeProject);
        }

        // Tools
        //--------------------------------------------------

        void initDefaultTools() {

            tools.clear();

            tools.push_back(Tool::GodTool(1.0, 1));
            tools.push_back(Tool::GodTool(3.0, 2));
            tools.push_back(Tool::GodTool(10.0, 3));

            selectedToolIndex = 0;
            toolFolderPath = "";
        }

        size_t toolCount() const {
            return tools.size();
        }

        Tool* toolAt(size_t index) {

            if (index >= tools.size()) {
                return nullptr;
            }

            return &tools[index];
        }

        Tool* selectedTool() {
            return toolAt(selectedToolIndex);
        }

        bool selectTool(size_t index) {

            if (index >= tools.size()) {
                return false;
            }

            selectedToolIndex = index;

            return true;
        }

        bool selectToolFolder() {

            Rev::OS::File folder;

            if (!folder.selectFolder(
                "Select Tool Folder",
                toolFolderPath
            )) {
                return false;
            }

            toolFolderPath = folder.pathname;

            return true;
        }

        Project* createProject(
            bool loadDefault = false,
            std::string name = "Untitled Project"
        ) {
            Project* project = new Project(loadDefault, name);

            projects.push_back(project);

            if (!activeProject) {
                activeProject = project;
            }

            return project;
        }

        Project* createEmptyProject(
            std::string name = "Untitled Project"
        ) {
            return createProject(false, name);
        }

        Project* newProject() {

            Project* project = createEmptyProject("Untitled Project");

            activeProject = project;

            saveSession();

            return project;
        }

        // Save / discard / cancel for one dirty project.
        // Returns false if the user cancelled or save failed.
        bool tryResolveDirtyProject(Project* project) {

            if (!project || !project->dirty) {
                return true;
            }

            Rev::OS::UnsavedChangesResult result =
                Rev::OS::Dialog::UnsavedChanges(project->name);

            if (result == Rev::OS::UnsavedChangesResult::Cancel) {
                return false;
            }

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

            auto it = std::find(
                projects.begin(),
                projects.end(),
                project
            );

            if (it == projects.end()) { return false; }

            if (!tryResolveDirtyProject(project)) {
                return false;
            }

            bool wasActive = (project == activeProject);

            size_t index = static_cast<size_t>(
                std::distance(projects.begin(), it)
            );

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

            if (!activeProject->save()) {
                return false;
            }

            saveSession();

            return true;
        }

        bool saveProjectAs() {

            if (!activeProject) { return false; }

            if (!activeProject->saveAs()) {
                return false;
            }

            saveSession();

            return true;
        }

        bool openProject() {

            Rev::OS::File selected;

            if (!selected.open(
                "Open CAM Project",
                "CAM Project\0*.cam\0JSON Files\0*.json\0All Files\0*.*\0"
            )) {
                return false;
            }

            Project* project = createEmptyProject("Untitled Project");

            if (!project->loadProjectFile(selected)) {

                closeProject(project);
                return false;
            }

            activeProject = project;

            saveSession();

            return true;
        }

        bool setActiveProject(Project* project) {

            if (!project) { return false; }

            auto it = std::find(
                projects.begin(),
                projects.end(),
                project
            );

            if (it == projects.end()) { return false; }

            activeProject = project;

            saveSession();

            return true;
        }

        bool setActiveProject(size_t index) {

            if (index >= projects.size()) { return false; }

            activeProject = projects[index];

            saveSession();

            return true;
        }

        // Active project forwarding
        //--------------------------------------------------

        Model* getDisplayedModel() {

            if (!activeProject) { return nullptr; }

            return activeProject->getDisplayedModel();
        }

        bool selectState(MaterialState* state) {

            if (!activeProject) { return false; }

            return activeProject->selectState(state);
        }

        bool deleteState(MaterialState* state) {

            if (!activeProject) { return false; }

            return activeProject->deleteState(state);
        }

        bool defeatureSelected() {

            if (!activeProject) { return false; }

            return activeProject->defeatureSelected();
        }

        bool commitWorkingState() {

            if (!activeProject) { return false; }

            return activeProject->commitWorkingState();
        }
    };
}