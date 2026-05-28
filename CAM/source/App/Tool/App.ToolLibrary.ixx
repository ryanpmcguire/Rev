module;

#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <filesystem>

#include <nlohmann/json.hpp>
#include <dbg.hpp>

export module Cam.App.ToolLibrary;

import Rev.OS.File;

import Cam.App.Tool;

export namespace Cam::App {

    using Json = nlohmann::json;

    struct ToolLibrary {

        // Storage
        std::unordered_map<std::string, Tool> byName;
        std::vector<std::string> order;

        // State
        //--------------------------------------------------

        void clear() {
            byName.clear();
            order.clear();
        }

        size_t size() const {
            return byName.size();
        }

        bool empty() const {
            return byName.empty();
        }

        const Tool* find(const std::string& name) const {

            if (name.empty()) { return nullptr; }

            auto it = byName.find(name);

            if (it == byName.end()) { return nullptr; }

            return &it->second;
        }

        Tool* find(const std::string& name) {

            if (name.empty()) { return nullptr; }

            auto it = byName.find(name);

            if (it == byName.end()) { return nullptr; }

            return &it->second;
        }

        const Tool* at(size_t index) const {

            if (index >= order.size()) { return nullptr; }

            return find(order[index]);
        }

        Tool* at(size_t index) {

            if (index >= order.size()) { return nullptr; }

            return find(order[index]);
        }

        // Mutation
        //--------------------------------------------------

        bool insertTool(const Tool& tool) {

            if (tool.name.empty()) { return false; }

            bool isNew = !byName.contains(tool.name);

            byName[tool.name] = tool;

            if (isNew) {
                order.push_back(tool.name);
            }

            return true;
        }

        bool removeTool(const std::string& name) {

            if (name.empty()) { return false; }

            auto it = byName.find(name);

            if (it == byName.end()) { return false; }

            byName.erase(it);

            order.erase(std::remove(order.begin(), order.end(), name), order.end());

            return true;
        }

        static std::string uniqueToolName(const ToolLibrary& library, const std::string& base = "New Tool") {

            if (!library.find(base)) {
                return base;
            }

            for (int i = 2; i < 1000; i++) {

                std::string candidate = base + " " + std::to_string(i);

                if (!library.find(candidate)) {
                    return candidate;
                }
            }

            return base + " " + std::to_string(library.size() + 1);
        }

        bool replaceTool(const std::string& keyName, const Tool& updated) {

            if (updated.name.empty()) { return false; }

            auto it = byName.find(keyName);

            if (it == byName.end()) { return false; }

            const std::string& newName = updated.name;

            if (newName != keyName) {

                if (byName.contains(newName)) { return false; }

                Tool tool = updated;
                byName.erase(it);
                byName.emplace(newName, std::move(tool));

                for (std::string& entry : order) {

                    if (entry == keyName) {
                        entry = newName;
                        break;
                    }
                }
            }
            else { it->second = updated; }

            return true;
        }

        // Paths
        //--------------------------------------------------

        static std::string defaultToolFolderPath() {

            Rev::OS::File folder = Rev::OS::File::FromPath({
                Rev::OS::File::KnownFolder::RoamingAppData,
                "CAM",
                "Tools",
                "General"
            });

            if (!folder) { return ""; }

            return folder.pathname;
        }

        static std::string toolFileName(const Tool& tool) {
            return tool.name + ".json";
        }

        static void assignDefaultToolFolderIfNeeded(std::string& toolFolderPath) {
            if (!toolFolderPath.empty()) { return; }

            toolFolderPath = defaultToolFolderPath();
        }

        // Serialization
        //--------------------------------------------------

        static Json toolToJson(const Tool& tool) {

            Json json;

            json["type"] = "Cam.Tool";
            json["version"] = 1;
            json["name"] = tool.name;
            json["kind"] = Tool::typeToKindString(tool.type);
            json["diameter"] = tool.diameter;
            json["radius"] = tool.radius;
            json["length"] = tool.length;
            json["axis"] = Json::array({
                tool.axis.x,
                tool.axis.y,
                tool.axis.z
            });

            return json;
        }

        static bool toolFromJson(const Json& json, Tool& out) {
            if (!json.is_object()) { return false; }

            if (json.contains("type") && json["type"].is_string() && json["type"].get<std::string>() != "Cam.Tool") {
                return false;
            }

            if (!json.contains("name") || !json["name"].is_string()) { return false; }

            out = Tool();
            out.name = json["name"].get<std::string>();

            std::string kind = "EndMill";

            if (json.contains("kind") && json["kind"].is_string()) {
                kind = json["kind"].get<std::string>();
            }

            if (kind != "EndMill" && kind != "ThreadMill" && kind != "Chamfer" && kind != "Cylinder") {
                return false;
            }

            out.type = Tool::typeFromKindString(kind);

            if (json.contains("diameter") && json["diameter"].is_number()) {
                out.diameter = json["diameter"].get<double>();
            }

            if (json.contains("radius") && json["radius"].is_number()) {
                out.radius = json["radius"].get<double>();
            }
            else { out.radius = out.diameter * 0.5; }

            if (json.contains("length") && json["length"].is_number()) {
                out.length = json["length"].get<double>();
            }

            if (json.contains("axis") && json["axis"].is_array() && json["axis"].size() >= 3) {
                out.axis.x = json["axis"][0].get<float>();
                out.axis.y = json["axis"][1].get<float>();
                out.axis.z = json["axis"][2].get<float>();
            }

            return true;
        }

        // File I/O
        //--------------------------------------------------

        static bool saveToolFileAtPath(const std::string& filePath, const Tool& tool) {
            if (filePath.empty()) { return false; }

            std::filesystem::path path(filePath);
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);

            Rev::OS::File file({
                .pathname = filePath
            });

            if (!file) { return false; }

            Json json = toolToJson(tool);

            return file.writeText(json.dump(4));
        }

        static bool saveToolFile(const std::string& folderPath, const Tool& tool) {
            if (folderPath.empty()) { return false; }

            std::filesystem::path dir(folderPath);

            return saveToolFileAtPath((dir / toolFileName(tool)).string(), tool);
        }

        static bool loadToolFile(const std::filesystem::path& filePath, Tool& out) {
            Rev::OS::File file({
                .pathname = filePath.string()
            });

            if (!file.exists) { return false; }

            std::string text;

            if (!file.readText(text) || text.empty()) { return false; }

            Json json;

            try {
                json = Json::parse(text);
            }

            catch (...) {
                return false;
            }

            return toolFromJson(json, out);
        }

        static std::vector<std::filesystem::path> listToolJsonFiles(const std::string& folderPath) {
            std::vector<std::filesystem::path> files;

            std::error_code ec;

            if (!std::filesystem::is_directory(folderPath, ec)) { return files; }

            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folderPath, ec)) {
                if (ec) { break; }

                if (!entry.is_regular_file()) { continue; }
                if (entry.path().extension() != ".json") { continue; }

                files.push_back(entry.path());
            }

            std::sort(files.begin(), files.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
                return a.filename().string() < b.filename().string();
            });

            return files;
        }

        // Load / bootstrap
        //--------------------------------------------------

        void createDefaultTools() {

            clear();

            insertTool(Tool::GodTool(1.0, 1));
            insertTool(Tool::GodTool(3.0, 2));
            insertTool(Tool::GodTool(10.0, 3));
        }

        void assignFilePathsFromFolder(const std::string& folderPath) {

            if (folderPath.empty()) { return; }

            std::filesystem::path dir(folderPath);

            for (const std::string& name : order) {

                Tool* tool = find(name);

                if (!tool) { continue; }

                tool->filePath = (dir / toolFileName(*tool)).string();
            }
        }

        bool saveAllTools(const std::string& folderPath) const {

            bool ok = true;

            for (const std::string& name : order) {

                auto it = byName.find(name);

                if (it == byName.end()) { continue; }
                if (!saveToolFile(folderPath, it->second)) { ok = false; }
            }

            return ok;
        }

        bool loadOrCreateDefaults(const std::string& folderPath) {

            clear();

            if (folderPath.empty()) {
                createDefaultTools();
                return false;
            }

            std::error_code ec;
            std::filesystem::create_directories(folderPath, ec);

            std::vector<std::filesystem::path> files =
                listToolJsonFiles(folderPath);

            if (files.empty()) {

                createDefaultTools();

                dbg(
                    "[ToolLibrary] No tools in %s — creating defaults",
                    folderPath.c_str()
                );

                if (!saveAllTools(folderPath)) {
                    return false;
                }

                assignFilePathsFromFolder(folderPath);

                return true;
            }

            for (const std::filesystem::path& filePath : files) {

                Tool tool;

                if (!loadToolFile(filePath, tool)) {
                    dbg(
                        "[ToolLibrary] Skipped invalid tool file: %s",
                        filePath.string().c_str()
                    );
                    continue;
                }

                tool.filePath = filePath.string();

                insertTool(tool);
            }

            if (empty()) {
                createDefaultTools();

                if (!saveAllTools(folderPath)) {
                    return false;
                }

                assignFilePathsFromFolder(folderPath);

                return true;
            }

            dbg(
                "[ToolLibrary] Loaded %zu tool(s) from %s",
                size(),
                folderPath.c_str()
            );

            return true;
        }
    };
}
