module;

#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>

export module Sketch.App;

import Rev.OS.File;

import Sketch.App.Project;

export namespace Sketch::App {

    // The active sketch tool. For now just the basic primitives; compound
    // shapes come later.
    enum class SketchTool {
        None,
        Point,
        Line,
        Arc,
        Circle
    };

    // The application-wide state: the open sketch documents and which one is
    // active. A trimmed mirror of the CAM AppState — just enough project/tab
    // lifecycle to drive the tab view. No persistence yet.
    struct AppState {

        std::vector<Project*> projects;
        Project* activeProject = nullptr;

        // Currently selected sketch tool (drives the toolbar highlight; the
        // actual drawing behaviour is wired up later).
        SketchTool activeTool = SketchTool::None;

        // Select a tool, toggling it off if it was already active.
        void selectTool(SketchTool tool) {
            activeTool = (activeTool == tool) ? SketchTool::None : tool;
        }

        // Create
        //--------------------------------------------------

        // Fetch (or lazily create) the AppState stored on a window's shared slot.
        static AppState* Get(void*& state) {

            AppState* app = static_cast<AppState*>(state);

            if (!app) { app = new AppState(); state = app; }

            return app;
        }

        AppState() {
            // Start with a single empty document so there is always something
            // to look at.
            newProject();
        }

        ~AppState() {

            for (Project* project : projects) {
                delete project;
            }

            projects.clear();
            activeProject = nullptr;
        }

        // Project lifecycle
        //--------------------------------------------------

        Project* createEmptyProject(std::string name = "Untitled Sketch") {

            Project* project = new Project(std::move(name));

            projects.push_back(project);

            if (!activeProject) { activeProject = project; }

            return project;
        }

        Project* newProject() {

            Project* project = createEmptyProject("Untitled Sketch");

            activeProject = project;

            return project;
        }

        // Open an existing `<Name>.sketch` workspace folder.
        bool openProject() {

            Rev::OS::File folder;

            if (!folder.selectFolder("Open Sketch Workspace")) { return false; }

            Project* project = createEmptyProject("Untitled Sketch");

            if (!project->loadFromFolder(folder.pathname)) {
                closeProject(project);
                return false;
            }

            activeProject = project;
            return true;
        }

        bool closeProject(Project* project) {

            if (!project) { return false; }

            auto it = std::find(projects.begin(), projects.end(), project);

            if (it == projects.end()) { return false; }

            bool wasActive = (project == activeProject);

            size_t index = static_cast<size_t>(std::distance(projects.begin(), it));

            projects.erase(it);

            delete project;

            if (projects.empty()) {
                activeProject = nullptr;
                return true;
            }

            if (wasActive) {

                if (index >= projects.size()) {
                    index = projects.size() - 1;
                }

                activeProject = projects[index];
            }

            return true;
        }

        bool setActiveProject(Project* project) {

            if (!project) { return false; }

            auto it = std::find(projects.begin(), projects.end(), project);

            if (it == projects.end()) { return false; }

            activeProject = project;

            return true;
        }

        size_t activeProjectIndex() const {

            for (size_t i = 0; i < projects.size(); i++) {
                if (projects[i] == activeProject) { return i; }
            }

            return 0;
        }

        bool saveProject() {

            if (!activeProject) { return false; }

            // First save (no path yet): fall back to choosing a location.
            if (activeProject->path.empty()) {
                return saveProjectAs();
            }

            return activeProject->save();
        }

        // Pick a parent directory and write a fresh `<Name>.sketch` workspace
        // folder inside it.
        bool saveProjectAs() {

            if (!activeProject) { return false; }

            Rev::OS::File parent;

            if (!parent.selectFolder("Choose Location for Sketch Workspace")) { return false; }

            std::filesystem::path folder =
                std::filesystem::path(parent.pathname) /
                Project::folderNameFor(activeProject->name);

            return activeProject->saveAs(folder.string());
        }

        // Shutdown
        //--------------------------------------------------

        bool confirmApplicationClose() {

            for (Project* project : projects) {
                delete project;
            }

            projects.clear();
            activeProject = nullptr;

            return true;
        }
    };
}
