module;

#include <string>
#include <vector>
#include <fstream>

#include <nlohmann/json.hpp>

export module Cam.App.Persist;

import Rev.OS.File;

import Cam.App.Project;

export namespace Cam::App {

    using Json = nlohmann::json;

    struct Persist {

        // File location
        //--------------------------------------------------

        static Rev::OS::File persistFile() {
            return Rev::OS::File::FromPath({
                Rev::OS::File::KnownFolder::RoamingAppData,
                "CAM",
                "persist.json"
            });
        }

        // Serialization
        //--------------------------------------------------

        static Json entryFromProject(const Project* project) {
            Json entry;

            if (!project) { return entry; }

            entry["name"] = project->name;

            if (project->hasProjectFile()) {
                entry["projectFile"] = project->projectFile.pathname;
            }
            else { entry["projectFile"] = ""; }

            if (project->hasFile()) {
                entry["sourceFile"] = project->file.pathname;
            }
            else { entry["sourceFile"] = ""; }

            entry["toolFolderPath"] = project->toolFolderPath;

            return entry;
        }

        static Json buildState(const std::vector<Project*>& projects, Project* activeProject) {
            Json json;

            json["type"] = "Cam.Persist";
            json["version"] = 1;

            size_t activeIndex = 0;

            for (size_t i = 0; i < projects.size(); i++) {
                if (projects[i] == activeProject) {
                    activeIndex = i;
                    break;
                }
            }

            json["activeProjectIndex"] = activeIndex;

            json["projects"] = Json::array();

            for (Project* project : projects) {
                json["projects"].push_back(entryFromProject(project));
            }

            return json;
        }

        // Save / load
        //--------------------------------------------------

        static bool save(const std::vector<Project*>& projects, Project* activeProject) {
            Rev::OS::File file = persistFile();

            if (!file) { return false; }

            Json json = buildState(projects, activeProject);

            return file.writeText(json.dump(4));
        }

        static bool loadEntry(const Json& entry, Project* project) {
            if (!project || !entry.is_object()) { return false; }

            // Prefer a saved .cam project file over a bare STEP source.
            if (entry.contains("projectFile") && entry["projectFile"].is_string()) {
                std::string projectPath = entry["projectFile"].get<std::string>();

                if (!projectPath.empty()) {

                    Rev::OS::File camFile({
                        .pathname = projectPath
                    });

                    if (camFile.exists) {
                        return project->loadProjectFile(camFile);
                    }
                }
            }

            // Fall back to loading a STEP file directly.
            if (entry.contains("sourceFile") && entry["sourceFile"].is_string()) {
                std::string sourcePath = entry["sourceFile"].get<std::string>();

                if (!sourcePath.empty()) {

                    Rev::OS::File stepFile({
                        .pathname = sourcePath
                    });

                    if (stepFile.exists) {
                        return project->loadStepFile(stepFile);
                    }
                }
            }

            if (entry.contains("toolFolderPath") && entry["toolFolderPath"].is_string()) {
                project->toolFolderPath = entry["toolFolderPath"].get<std::string>();
            }

            return false;
        }

        static bool load(std::vector<Project*>& outProjects, Project*& outActiveProject) {
            outProjects.clear();
            outActiveProject = nullptr;

            Rev::OS::File file = persistFile();

            if (!file) { return false; }

            std::string text;

            if (!file.readText(text) || text.empty()) { return false; }

            Json json;

            try {
                json = Json::parse(text);
            }

            catch (...) {
                return false;
            }

            if (!json.is_object()) { return false; }

            if (json.contains("type") && json["type"].is_string() && json["type"].get<std::string>() != "Cam.Persist") {
                return false;
            }

            if (!json.contains("projects") || !json["projects"].is_array()) {
                return false;
            }

            const Json& projectsJson = json["projects"];

            if (projectsJson.empty()) {
                return true;
            }

            for (const Json& entry : projectsJson) {

                std::string name = "Untitled Project";

                if (entry.contains("name") && entry["name"].is_string()) {
                    name = entry["name"].get<std::string>();
                }

                Project* project = new Project(false, name);

                if (!loadEntry(entry, project)) {
                    delete project;
                    continue;
                }

                outProjects.push_back(project);
            }

            if (outProjects.empty()) {
                return false;
            }

            size_t activeIndex = 0;

            if (json.contains("activeProjectIndex")) {
                activeIndex = json["activeProjectIndex"].get<size_t>();
            }

            if (activeIndex >= outProjects.size()) {
                activeIndex = 0;
            }

            outActiveProject = outProjects[activeIndex];

            return true;
        }
    };
}
