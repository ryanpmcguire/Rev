module;

#include <string>
#include <vector>
#include <algorithm>

#include <managed.hpp>

export module Cam.App;

import Cam.App.Project;
import Cam.App.MaterialState;
import Cam.App.Model;

export namespace Cam::App {

    struct AppState {

        std::vector<Project*> projects;
        Project* activeProject = nullptr;

        static AppState* Get(void*& state) {

            AppState* app = static_cast<AppState*>(state);

            if (!app) {
                app = new AppState();
                state = app;
            }

            return app;
        }

        AppState() {
            createInitialProjects();
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

        void createInitialProjects() {

            projects.clear();
            activeProject = nullptr;

            Project* loadedProject = createProject(true);
            Project* emptyProject = createProject(false);

            activeProject = loadedProject;
        }

        Project* createProject(
            bool loadDefault = true
        ) {
            Project* project = new Project(loadDefault);

            projects.push_back(project);

            if (!activeProject) {
                activeProject = project;
            }

            return project;
        }

        Project* createEmptyProject() {
            return createProject(false);
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

            return true;
        }

        bool setActiveProject(size_t index) {

            if (index >= projects.size()) { return false; }

            activeProject = projects[index];

            return true;
        }

        size_t activeProjectIndex() const {

            for (size_t i = 0; i < projects.size(); i++) {
                if (projects[i] == activeProject) { return i; }
            }

            return 0;
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