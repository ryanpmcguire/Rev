module;

#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>

export module Sketch.App;

import Rev.OS.File;

import Sketch.App.Project;
import Sketch.App.Persist;

export namespace Sketch::App {

    // The active sketch tool. For now just the basic primitives; compound
    // shapes come later.
    enum class SketchTool {
        None,
        Point,
        Line,
        Arc,
        Circle,
        Box,
        Ellipse,
        EllipseArc
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

        // Generic display setting: stroke width (px) for sketch geometry.
        float lineThickness = 2.0f;

        // Offset display options (the toolbar's right-hand view-select group).
        bool viewValid     = true;    // final valid chains (red/green by signed area)
        bool viewWinding   = false;   // debug: crossing-number level colours + magenta + marks
        bool viewDiscarded = false;   // debug: include the discarded (non-minimum) fragments
        bool viewArrows    = true;    // travel-direction arrows on offset chains
        bool viewToolpath  = true;    // apply toolpath rules (sanity checks, step list) over the method's output

        // Toolpathing parameters (the left-hand parameter panel). The strategy
        // names a MOCK strategy implemented in the sketch app, so strategies can
        // be exercised here before they integrate into the CAM app.
        float toolRadius = 1.0f;
        int   iterations = 32;        // max offset generations per run
        bool  toolReverse = false;    // execute the final toolpath backwards (chain ORDER only)
        bool  climbMilling = true;    // climb vs conventional: the HANDEDNESS of every toolpath chain
        std::string strategy = "profile";

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
            loadSessionOrDefault();
        }

        // Session
        //--------------------------------------------------

        void saveSession() {
            Persist::save(projects, activeProject);
        }

        // Reopen the previous session's workspaces, or start fresh if there is no
        // usable session.
        void loadSessionOrDefault() {
            if (!Persist::load(projects, activeProject)) {
                newProject();
            }
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

            saveSession();

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
            saveSession();
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

        bool setActiveProject(Project* project) {

            if (!project) { return false; }

            auto it = std::find(projects.begin(), projects.end(), project);

            if (it == projects.end()) { return false; }

            activeProject = project;

            saveSession();

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

            if (!activeProject->save()) { return false; }

            saveSession();
            return true;
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

            if (!activeProject->saveAs(folder.string())) { return false; }

            saveSession();
            return true;
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
