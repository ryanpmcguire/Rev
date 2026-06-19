module;

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <filesystem>
#include <cctype>

#include <nlohmann/json.hpp>
#include <dbg.hpp>

export module Cam.App.MachineLibrary;

import Rev.OS.File;

import Cam.App.Model;
import Cam.App.MachineProfile;

export namespace Cam::App {

    using Json = nlohmann::json;

    struct MachineLibrary {

        std::unordered_map<std::string, MachineProfile> byName;
        std::vector<std::string> order;

        void clear() {
            byName.clear();
            order.clear();
        }

        size_t size() const { return byName.size(); }
        bool empty() const { return byName.empty(); }

        const MachineProfile* find(const std::string& name) const {
            if (name.empty()) { return nullptr; }
            auto it = byName.find(name);
            if (it == byName.end()) { return nullptr; }
            return &it->second;
        }

        MachineProfile* find(const std::string& name) {
            if (name.empty()) { return nullptr; }
            auto it = byName.find(name);
            if (it == byName.end()) { return nullptr; }
            return &it->second;
        }

        const MachineProfile* at(size_t index) const {
            if (index >= order.size()) { return nullptr; }
            return find(order[index]);
        }

        MachineProfile* at(size_t index) {
            if (index >= order.size()) { return nullptr; }
            return find(order[index]);
        }

        bool insertMachine(const MachineProfile& machine) {
            if (machine.name.empty()) { return false; }

            bool isNew = !byName.contains(machine.name);
            byName[machine.name] = machine;

            if (isNew) {
                order.push_back(machine.name);
            }

            return true;
        }

        bool removeMachine(const std::string& name) {
            if (name.empty()) { return false; }

            auto it = byName.find(name);
            if (it == byName.end()) { return false; }

            byName.erase(it);
            order.erase(std::remove(order.begin(), order.end(), name), order.end());

            return true;
        }

        static std::string uniqueMachineName(
            const MachineLibrary& library,
            const std::string& base = "New Machine"
        ) {
            if (!library.find(base)) { return base; }

            for (int i = 2; i < 1000; i++) {
                std::string candidate = base + " " + std::to_string(i);
                if (!library.find(candidate)) { return candidate; }
            }

            return base + " " + std::to_string(library.size() + 1);
        }

        bool replaceMachine(const std::string& keyName, const MachineProfile& updated) {
            if (updated.name.empty()) { return false; }

            auto it = byName.find(keyName);
            if (it == byName.end()) { return false; }

            const std::string& newName = updated.name;

            if (newName != keyName) {
                if (byName.contains(newName)) { return false; }

                MachineProfile machine = updated;
                byName.erase(it);
                byName.emplace(newName, std::move(machine));

                for (std::string& entry : order) {
                    if (entry == keyName) {
                        entry = newName;
                        break;
                    }
                }
            }
            else {
                it->second = updated;
            }

            return true;
        }

        // Parent directory that contains one subfolder per machine definition.
        static std::string machinesRootPath() {
            Rev::OS::File folder = Rev::OS::File::FromPath({
                Rev::OS::File::KnownFolder::RoamingAppData,
                "CAM",
                "Machines"
            });

            if (!folder) { return ""; }

            return folder.pathname;
        }

        static void normalizeMachinesRoot(std::string& path) {
            if (path.empty()) { return; }

            std::filesystem::path p(path);
            const std::string leaf = p.filename().string();

            if (leaf == "General") {
                path = p.parent_path().string();
            }
        }

        static void assignDefaultMachinesRootIfNeeded(std::string& machinesRootPath) {
            if (!machinesRootPath.empty()) {
                normalizeMachinesRoot(machinesRootPath);
                return;
            }

            machinesRootPath = MachineLibrary::machinesRootPath();
        }

        // "Carvera Air" -> "carvera air"
        static std::string machineDirectoryName(const std::string& displayName) {
            std::string out;
            out.reserve(displayName.size());

            bool lastWasSpace = false;

            for (unsigned char ch : displayName) {
                if (std::isspace(ch)) {
                    if (!out.empty() && !lastWasSpace) {
                        out.push_back(' ');
                        lastWasSpace = true;
                    }
                }
                else if (std::isalnum(ch) || ch == '-' || ch == '_') {
                    out.push_back(static_cast<char>(std::tolower(ch)));
                    lastWasSpace = false;
                }
            }

            while (!out.empty() && out.back() == ' ') {
                out.pop_back();
            }

            if (out.empty()) { out = "machine"; }

            return out;
        }

        static std::string machineFolderPath(
            const std::string& machinesRoot,
            const std::string& displayName
        ) {
            if (machinesRoot.empty()) { return ""; }

            return (std::filesystem::path(machinesRoot) / machineDirectoryName(displayName)).string();
        }

        static std::string machineJsonPath(
            const std::string& machinesRoot,
            const MachineProfile& machine
        ) {
            return (std::filesystem::path(machineFolderPath(machinesRoot, machine.name))
                / machineFileName(machine)).string();
        }

        static std::string machineFileName(const MachineProfile& machine) {
            return machine.name + ".machine.json";
        }

        static bool ensureMachineFolder(const std::string& folderPath) {
            if (folderPath.empty()) { return false; }

            std::error_code ec;
            std::filesystem::create_directories(folderPath, ec);

            return !ec;
        }

        static bool renameMachineFolder(
            const std::string& machinesRoot,
            const std::string& oldDisplayName,
            const std::string& newDisplayName
        ) {
            const std::string oldFolder = machineFolderPath(machinesRoot, oldDisplayName);
            const std::string newFolder = machineFolderPath(machinesRoot, newDisplayName);

            if (oldFolder.empty() || newFolder.empty() || oldFolder == newFolder) {
                return true;
            }

            std::error_code ec;

            if (!std::filesystem::exists(oldFolder, ec)) {
                return ensureMachineFolder(newFolder);
            }

            if (std::filesystem::exists(newFolder, ec)) {
                return ensureMachineFolder(newFolder);
            }

            std::filesystem::rename(oldFolder, newFolder, ec);

            if (ec) {
                ensureMachineFolder(newFolder);
                return false;
            }

            return true;
        }

        static std::string stepAssetName(
            const char* role,
            const std::filesystem::path& sourcePath
        ) {
            std::string ext = sourcePath.extension().string();
            if (ext.empty()) { ext = ".step"; }

            return std::string(role) + ext;
        }

        static bool copyStepAsset(
            const std::string& folderPath,
            const char* role,
            const std::string& sourcePath,
            std::string& outRelativeName
        ) {
            if (folderPath.empty() || sourcePath.empty()) { return false; }

            std::filesystem::path source(sourcePath);
            if (!std::filesystem::exists(source)) { return false; }

            std::filesystem::path destName = stepAssetName(role, source);
            std::filesystem::path destPath = std::filesystem::path(folderPath) / destName;

            std::error_code ec;
            ensureMachineFolder(folderPath);
            std::filesystem::copy_file(
                source,
                destPath,
                std::filesystem::copy_options::overwrite_existing,
                ec
            );

            if (ec) { return false; }

            outRelativeName = destName.string();
            return true;
        }

        static Json machineToJson(const MachineProfile& machine) {
            Json json;

            json["type"] = "Cam.Machine";
            json["version"] = 1;
            json["name"] = machine.name;

            json["spindle"] = Json::object();
            json["spindle"]["minRpm"] = machine.spindleMinRpm;
            json["spindle"]["maxRpm"] = machine.spindleMaxRpm;

            json["axes"] = Json::object();
            json["axes"]["linear"] = Json::object();
            json["axes"]["linear"]["x"] = machine.axisX;
            json["axes"]["linear"]["y"] = machine.axisY;
            json["axes"]["linear"]["z"] = machine.axisZ;
            json["axes"]["rotary"] = Json::object();
            json["axes"]["rotary"]["x"] = machine.rotaryX;
            json["axes"]["rotary"]["y"] = machine.rotaryY;
            json["axes"]["rotary"]["z"] = machine.rotaryZ;

            json["rotaryAxis"] = Json::object();
            json["rotaryAxis"]["x"] = machine.rotaryAxisX;
            json["rotaryAxis"]["y"] = machine.rotaryAxisY;
            json["rotaryAxis"]["z"] = machine.rotaryAxisZ;
            json["rotaryAxis"]["dirX"] = machine.rotaryAxisDirX;
            json["rotaryAxis"]["dirY"] = machine.rotaryAxisDirY;
            json["rotaryAxis"]["dirZ"] = machine.rotaryAxisDirZ;
            json["rotaryAxis"]["calibrated"] = machine.rotaryAxisCalibrated;
            json["rotaryAxis"]["sigma"] = machine.rotaryAxisSigma;

            json["stepFiles"] = Json::object();
            json["stepFiles"]["spindle"] = machine.spindleStep;
            json["stepFiles"]["bed"] = machine.bedStep;
            json["stepFiles"]["workpiece"] = machine.workpieceStep;
            json["stepFiles"]["rotary"] = machine.rotaryStep;
            json["stepFiles"]["rotaryBody"] = machine.rotaryBodyStep;

            return json;
        }

        static bool machineFromJson(const Json& json, MachineProfile& out) {
            if (!json.is_object()) { return false; }

            if (json.contains("type") && json["type"].is_string()
                && json["type"].get<std::string>() != "Cam.Machine") {
                return false;
            }

            if (!json.contains("name") || !json["name"].is_string()) { return false; }

            out = MachineProfile();
            out.name = json["name"].get<std::string>();

            if (json.contains("spindle") && json["spindle"].is_object()) {
                const Json& s = json["spindle"];
                if (s.contains("minRpm") && s["minRpm"].is_number()) {
                    out.spindleMinRpm = s["minRpm"].get<double>();
                }
                if (s.contains("maxRpm") && s["maxRpm"].is_number()) {
                    out.spindleMaxRpm = s["maxRpm"].get<double>();
                }
            }

            if (json.contains("axes") && json["axes"].is_object()) {
                const Json& axes = json["axes"];

                if (axes.contains("linear") && axes["linear"].is_object()) {
                    const Json& linear = axes["linear"];
                    if (linear.contains("x") && linear["x"].is_boolean()) { out.axisX = linear["x"].get<bool>(); }
                    if (linear.contains("y") && linear["y"].is_boolean()) { out.axisY = linear["y"].get<bool>(); }
                    if (linear.contains("z") && linear["z"].is_boolean()) { out.axisZ = linear["z"].get<bool>(); }
                }

                if (axes.contains("rotary") && axes["rotary"].is_object()) {
                    const Json& rotary = axes["rotary"];
                    if (rotary.contains("x") && rotary["x"].is_boolean()) { out.rotaryX = rotary["x"].get<bool>(); }
                    if (rotary.contains("y") && rotary["y"].is_boolean()) { out.rotaryY = rotary["y"].get<bool>(); }
                    if (rotary.contains("z") && rotary["z"].is_boolean()) { out.rotaryZ = rotary["z"].get<bool>(); }
                }
            }

            if (json.contains("rotaryAxis") && json["rotaryAxis"].is_object()) {
                const Json& ra = json["rotaryAxis"];
                if (ra.contains("x") && ra["x"].is_number()) { out.rotaryAxisX = ra["x"].get<double>(); }
                if (ra.contains("y") && ra["y"].is_number()) { out.rotaryAxisY = ra["y"].get<double>(); }
                if (ra.contains("z") && ra["z"].is_number()) { out.rotaryAxisZ = ra["z"].get<double>(); }
                if (ra.contains("dirX") && ra["dirX"].is_number()) { out.rotaryAxisDirX = ra["dirX"].get<double>(); }
                if (ra.contains("dirY") && ra["dirY"].is_number()) { out.rotaryAxisDirY = ra["dirY"].get<double>(); }
                if (ra.contains("dirZ") && ra["dirZ"].is_number()) { out.rotaryAxisDirZ = ra["dirZ"].get<double>(); }
                if (ra.contains("calibrated") && ra["calibrated"].is_boolean()) { out.rotaryAxisCalibrated = ra["calibrated"].get<bool>(); }
                if (ra.contains("sigma") && ra["sigma"].is_number()) { out.rotaryAxisSigma = ra["sigma"].get<double>(); }
            }

            if (json.contains("stepFiles") && json["stepFiles"].is_object()) {
                const Json& steps = json["stepFiles"];
                if (steps.contains("spindle") && steps["spindle"].is_string()) {
                    out.spindleStep = steps["spindle"].get<std::string>();
                }
                if (steps.contains("bed") && steps["bed"].is_string()) {
                    out.bedStep = steps["bed"].get<std::string>();
                }
                if (steps.contains("workpiece") && steps["workpiece"].is_string()) {
                    out.workpieceStep = steps["workpiece"].get<std::string>();
                }
                if (steps.contains("rotary") && steps["rotary"].is_string()) {
                    out.rotaryStep = steps["rotary"].get<std::string>();
                }
                if (steps.contains("rotaryBody") && steps["rotaryBody"].is_string()) {
                    out.rotaryBodyStep = steps["rotaryBody"].get<std::string>();
                }
            }

            return true;
        }

        static bool saveMachineFileAtPath(const std::string& filePath, const MachineProfile& machine) {
            if (filePath.empty()) { return false; }

            std::filesystem::path path(filePath);
            ensureMachineFolder(path.parent_path().string());

            Rev::OS::File file({ .pathname = filePath });
            if (!file) { return false; }

            Json json = machineToJson(machine);
            return file.writeText(json.dump(4));
        }

        static bool loadMachineFile(const std::filesystem::path& filePath, MachineProfile& out) {
            Rev::OS::File file({ .pathname = filePath.string() });
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

            return machineFromJson(json, out);
        }

        static bool isMachineJsonFile(const std::filesystem::path& filePath) {
            const std::string filename = filePath.filename().string();
            if (filename.size() < 13) { return false; }
            return filename.substr(filename.size() - 13) == ".machine.json";
        }

        static void collectMachineJsonFilesInDirectory(
            const std::filesystem::path& directory,
            std::vector<std::filesystem::path>& out
        ) {
            std::error_code ec;

            if (!std::filesystem::is_directory(directory, ec)) { return; }

            for (const std::filesystem::directory_entry& entry :
                std::filesystem::directory_iterator(directory, ec)) {

                if (ec) { break; }
                if (!entry.is_regular_file()) { continue; }
                if (!isMachineJsonFile(entry.path())) { continue; }

                out.push_back(entry.path());
            }
        }

        static std::vector<std::filesystem::path> discoverMachineJsonFiles(const std::string& machinesRoot) {
            std::vector<std::filesystem::path> files;

            if (machinesRoot.empty()) { return files; }

            std::error_code ec;
            const std::filesystem::path root(machinesRoot);

            if (!std::filesystem::is_directory(root, ec)) { return files; }

            // Legacy flat layout in the root or General/ subfolder.
            collectMachineJsonFilesInDirectory(root, files);
            collectMachineJsonFilesInDirectory(root / "General", files);

            for (const std::filesystem::directory_entry& entry :
                std::filesystem::directory_iterator(root, ec)) {

                if (ec) { break; }
                if (!entry.is_directory()) { continue; }

                collectMachineJsonFilesInDirectory(entry.path(), files);
            }

            std::sort(files.begin(), files.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
                return a.string() < b.string();
            });

            files.erase(
                std::unique(files.begin(), files.end()),
                files.end()
            );

            return files;
        }

        static std::string storageFolderForProfile(const MachineProfile& machine) {
            if (!machine.filePath.empty()) {
                return std::filesystem::path(machine.filePath).parent_path().string();
            }

            return "";
        }

        void createDefaultMachine() {
            clear();

            MachineProfile machine;
            machine.name = "Carvera Air";
            machine.rotaryX = true;
            insertMachine(machine);
        }

        bool saveDefaultMachine(const std::string& machinesRoot) {
            if (empty()) { return false; }

            MachineProfile* machine = find(order.front());
            if (!machine) { return false; }

            const std::string folderPath = machineFolderPath(machinesRoot, machine->name);
            if (!ensureMachineFolder(folderPath)) { return false; }

            machine->filePath = machineJsonPath(machinesRoot, *machine);
            return saveMachineFileAtPath(machine->filePath, *machine);
        }

        bool loadFromMachinesRoot(const std::string& machinesRoot) {
            clear();

            if (machinesRoot.empty()) {
                createDefaultMachine();
                return false;
            }

            ensureMachineFolder(machinesRoot);

            std::vector<std::filesystem::path> files = discoverMachineJsonFiles(machinesRoot);

            if (files.empty()) {
                createDefaultMachine();

                dbg("[MachineLibrary] No machines under %s - creating Carvera Air", machinesRoot.c_str());

                if (!saveDefaultMachine(machinesRoot)) { return false; }

                reloadAllStepModels();
                return true;
            }

            for (const std::filesystem::path& filePath : files) {
                MachineProfile machine;

                if (!loadMachineFile(filePath, machine)) {
                    dbg("[MachineLibrary] Skipped invalid machine file: %s", filePath.string().c_str());
                    continue;
                }

                machine.filePath = filePath.string();
                insertMachine(machine);
            }

            if (empty()) {
                createDefaultMachine();
                if (!saveDefaultMachine(machinesRoot)) { return false; }

                reloadAllStepModels();
                return true;
            }

            dbg("[MachineLibrary] Loaded %zu machine(s) from %s", size(), machinesRoot.c_str());

            reloadAllStepModels();
            return true;
        }

        static void reloadSpindleModel(MachineProfile& machine) {
            machine.spindleModel.clear();
            machine.spindleMeshRevision++;

            if (machine.spindleStep.empty()) {
                return;
            }

            const std::string path = resolveStepPathForProfile(machine, machine.spindleStep);
            if (path.empty()) {
                return;
            }

            Rev::OS::File file({ .pathname = path });
            if (!file.exists) {
                dbg(
                    "[MachineLibrary] Spindle STEP missing for \"%s\": %s",
                    machine.name.c_str(),
                    path.c_str()
                );
                return;
            }

            try {
                machine.spindleModel = Model::FromStep(file);
            }
            catch (...) {
                dbg(
                    "[MachineLibrary] Failed to load spindle STEP for \"%s\": %s",
                    machine.name.c_str(),
                    path.c_str()
                );
                machine.spindleModel.clear();
                machine.spindleMeshRevision++;
                return;
            }

            if (machine.spindleModel.render.triangles.empty()) {
                dbg(
                    "[MachineLibrary] Spindle STEP produced no triangles for \"%s\"",
                    machine.name.c_str()
                );
                machine.spindleModel.clear();
                machine.spindleMeshRevision++;
                return;
            }

            machine.spindleMeshRevision++;

            dbg(
                "[MachineLibrary] Loaded spindle model for \"%s\" (%zu vertices)",
                machine.name.c_str(),
                machine.spindleModel.render.triangles.size()
            );
        }

        // Load the rotary-axis fixture (the chuck) from its STEP.  Mirrors the
        // spindle loader: the STEP's own origin is the centre of the +X-facing
        // cylinder face, so the model is placed UNTRANSFORMED here and positioned at
        // the rotary axis frame by the view.
        static void reloadRotaryModel(MachineProfile& machine) {
            machine.rotaryModel.clear();
            machine.rotaryMeshRevision++;

            if (machine.rotaryStep.empty()) { return; }

            const std::string path = resolveStepPathForProfile(machine, machine.rotaryStep);
            if (path.empty()) { return; }

            Rev::OS::File file({ .pathname = path });
            if (!file.exists) {
                dbg("[MachineLibrary] Rotary STEP missing for \"%s\": %s",
                    machine.name.c_str(), path.c_str());
                return;
            }

            try {
                machine.rotaryModel = Model::FromStep(file);
            }
            catch (...) {
                dbg("[MachineLibrary] Failed to load rotary STEP for \"%s\": %s",
                    machine.name.c_str(), path.c_str());
                machine.rotaryModel.clear();
                machine.rotaryMeshRevision++;
                return;
            }

            if (machine.rotaryModel.render.triangles.empty()) {
                dbg("[MachineLibrary] Rotary STEP produced no triangles for \"%s\"",
                    machine.name.c_str());
                machine.rotaryModel.clear();
                machine.rotaryMeshRevision++;
                return;
            }

            machine.rotaryMeshRevision++;
            dbg("[MachineLibrary] Loaded rotary model for \"%s\" (%zu vertices)",
                machine.name.c_str(), machine.rotaryModel.render.triangles.size());
        }

        // The rotary BODY/housing: same load path as the chuck, different asset.
        static void reloadRotaryBodyModel(MachineProfile& machine) {
            machine.rotaryBodyModel.clear();
            machine.rotaryBodyMeshRevision++;

            if (machine.rotaryBodyStep.empty()) { return; }

            const std::string path = resolveStepPathForProfile(machine, machine.rotaryBodyStep);
            if (path.empty()) { return; }

            Rev::OS::File file({ .pathname = path });
            if (!file.exists) {
                dbg("[MachineLibrary] Rotary BODY STEP missing for \"%s\": %s",
                    machine.name.c_str(), path.c_str());
                return;
            }

            try {
                machine.rotaryBodyModel = Model::FromStep(file);
            }
            catch (...) {
                dbg("[MachineLibrary] Failed to load rotary BODY STEP for \"%s\": %s",
                    machine.name.c_str(), path.c_str());
                machine.rotaryBodyModel.clear();
                machine.rotaryBodyMeshRevision++;
                return;
            }

            if (machine.rotaryBodyModel.render.triangles.empty()) {
                dbg("[MachineLibrary] Rotary BODY STEP produced no triangles for \"%s\"",
                    machine.name.c_str());
                machine.rotaryBodyModel.clear();
                machine.rotaryBodyMeshRevision++;
                return;
            }

            machine.rotaryBodyMeshRevision++;
            dbg("[MachineLibrary] Loaded rotary BODY model for \"%s\" (%zu vertices)",
                machine.name.c_str(), machine.rotaryBodyModel.render.triangles.size());
        }

        void reloadAllStepModels() {
            for (const std::string& name : order) {
                MachineProfile* machine = find(name);
                if (machine) {
                    reloadSpindleModel(*machine);
                    reloadRotaryModel(*machine);
                    reloadRotaryBodyModel(*machine);
                }
            }
        }

        static std::string resolveStepPath(
            const std::string& folderPath,
            const std::string& relativeName
        ) {
            if (folderPath.empty() || relativeName.empty()) { return ""; }
            return (std::filesystem::path(folderPath) / relativeName).string();
        }

        static std::string resolveStepPathForProfile(
            const MachineProfile& machine,
            const std::string& relativeName
        ) {
            return resolveStepPath(storageFolderForProfile(machine), relativeName);
        }

        static bool applyStepSources(
            const std::string& folderPath,
            MachineProfile& machine,
            const MachineStepSources& sources
        ) {
            bool ok = true;

            if (!sources.spindle.empty()) {
                if (!copyStepAsset(folderPath, "spindle", sources.spindle, machine.spindleStep)) {
                    ok = false;
                }
            }
            if (!sources.bed.empty()) {
                if (!copyStepAsset(folderPath, "bed", sources.bed, machine.bedStep)) {
                    ok = false;
                }
            }
            if (!sources.workpiece.empty()) {
                if (!copyStepAsset(folderPath, "workpiece", sources.workpiece, machine.workpieceStep)) {
                    ok = false;
                }
            }
            if (!sources.rotary.empty()) {
                if (!copyStepAsset(folderPath, "rotary", sources.rotary, machine.rotaryStep)) {
                    ok = false;
                }
            }
            if (!sources.rotaryBody.empty()) {
                if (!copyStepAsset(folderPath, "rotaryBody", sources.rotaryBody, machine.rotaryBodyStep)) {
                    ok = false;
                }
            }

            return ok;
        }
    };
}
