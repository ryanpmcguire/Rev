module;

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
            createProject();
        }

        ~AppState() {

            for (Project* project : projects) {
                delete project;
            }

            projects.clear();
            activeProject = nullptr;
        }

        Project* createProject() {

            Project* project = new Project();

            projects.push_back(project);
            activeProject = project;

            return project;
        }

        bool setActiveProject(Project* project) {

            if (!project) { return false; }

            activeProject = project;

            return true;
        }

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