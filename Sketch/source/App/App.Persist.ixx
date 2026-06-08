module;

#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <dbg.hpp>

export module Sketch.App.Persist;

import Rev.OS.File;

import Sketch.App.Project;

export namespace Sketch::App {

    using Json = nlohmann::json;

    // Session persistence: remembers which `.sketch` workspaces were open (by
    // folder path) and which was active, so they reopen on the next run. Only
    // saved projects (those with a path on disk) are remembered; an unsaved,
    // never-saved project has nothing to reopen.
    struct Persist {

        static Rev::OS::File persistFile() {
            return Rev::OS::File::FromPath({
                Rev::OS::File::KnownFolder::RoamingAppData,
                "Sketch",
                "persist.json"
            });
        }

        static bool save(const std::vector<Project*>& projects, Project* active) {

            Rev::OS::File file = persistFile();
            if (!file) { return false; }

            Json json;
            json["type"] = "Sketch.Persist";
            json["version"] = 1;
            json["projects"] = Json::array();

            size_t activeIndex = 0;
            size_t savedCount = 0;

            for (Project* project : projects) {

                if (!project || project->path.empty()) { continue; }   // not on disk

                json["projects"].push_back(project->path);

                if (project == active) { activeIndex = savedCount; }
                savedCount++;
            }

            json["activeProjectIndex"] = activeIndex;

            return file.writeText(json.dump(2));
        }

        // Rebuild the project list from the session. Returns false (and leaves the
        // outputs empty) if there is no usable session, so the caller can start
        // with a fresh project.
        static bool load(std::vector<Project*>& outProjects, Project*& outActive) {

            outProjects.clear();
            outActive = nullptr;

            Rev::OS::File file = persistFile();
            if (!file) { return false; }

            std::string text;
            if (!file.readText(text) || text.empty()) { return false; }

            Json json;
            try { json = Json::parse(text); }
            catch (...) { return false; }

            if (!json.is_object()) { return false; }
            if (!json.contains("projects") || !json["projects"].is_array()) { return false; }

            for (const Json& entry : json["projects"]) {

                if (!entry.is_string()) { continue; }
                std::string path = entry.get<std::string>();
                if (path.empty()) { continue; }

                Project* project = new Project();

                if (!project->loadFromFolder(path)) {
                    dbg("[Persist] could not reopen workspace: %s", path.c_str());
                    delete project;
                    continue;
                }

                outProjects.push_back(project);
            }

            if (outProjects.empty()) { return false; }

            size_t activeIndex = 0;
            if (json.contains("activeProjectIndex")) {
                try { activeIndex = json["activeProjectIndex"].get<size_t>(); }
                catch (...) { activeIndex = 0; }
            }
            if (activeIndex >= outProjects.size()) { activeIndex = 0; }

            outActive = outProjects[activeIndex];
            return true;
        }
    };
}
