module;

#include <array>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

export module Rev.OS.File;

export namespace Rev::OS {

    struct File {

        enum class KnownFolder {
            RoamingAppData,
            LocalAppData,
            CommonAppData,
            UserProfile,
            Documents,
            Desktop,
            Downloads,
            ProgramFiles,
            ProgramFilesX86,
        };

        using PathComponent = std::variant<KnownFolder, std::string>;

        inline static size_t maxBufferSize = 1024 * 1024;

        std::filesystem::path path;
        bool valid = false;

        bool exists = false;
        bool isDirectory = false;
        bool buffered = false;
        bool tooLargeToBuffer = false;

        uintmax_t size = 0;

        std::chrono::system_clock::time_point created = {};
        std::chrono::system_clock::time_point accessed = {};
        std::chrono::system_clock::time_point modified = {};

        std::string mime = "";
        std::string ext = "";
        std::string name = "";
        std::string pathname = "";

        std::vector<unsigned char> buffer;

        char* chars = nullptr;
        unsigned char* bytes = nullptr;
        void* data = nullptr;

        struct ConstructParams {
            std::string pathname = "";
            std::vector<PathComponent> path = {};
        };

        File() {}

        File(ConstructParams p) {
            if (!p.path.empty()) {
                path = resolvePath(p.path);
                valid = !path.empty();
            }
            else {
                path = std::filesystem::path(p.pathname);
                valid = !p.pathname.empty();
            }
            refresh();
        }

        File(std::initializer_list<PathComponent> pathComponents)
            : File({ .path = std::vector<PathComponent>(pathComponents) }) {}

        static std::filesystem::path knownFolderRoot(KnownFolder folder) {
            const char* home = std::getenv("HOME");
            std::filesystem::path homePath = home ? home : "";

            switch (folder) {
                case KnownFolder::RoamingAppData:
                case KnownFolder::LocalAppData: {
                    const char* xdg = std::getenv("XDG_CONFIG_HOME");
                    if (xdg && *xdg) return xdg;
                    return homePath / ".config";
                }
                case KnownFolder::CommonAppData:
                    return "/var/lib";
                case KnownFolder::UserProfile:
                    return homePath;
                case KnownFolder::Documents:
                    return homePath / "Documents";
                case KnownFolder::Desktop:
                    return homePath / "Desktop";
                case KnownFolder::Downloads:
                    return homePath / "Downloads";
                case KnownFolder::ProgramFiles:
                case KnownFolder::ProgramFilesX86:
                    return "/usr/local";
                default:
                    return {};
            }
        }

        static std::filesystem::path resolvePath(const std::vector<PathComponent>& components) {
            if (components.empty()) return {};

            std::filesystem::path result;
            bool hasKnownFolderRoot = false;

            for (const PathComponent& component : components) {
                if (std::holds_alternative<KnownFolder>(component)) {
                    if (hasKnownFolderRoot) return {};
                    result = knownFolderRoot(std::get<KnownFolder>(component));
                    if (result.empty()) return {};
                    hasKnownFolderRoot = true;
                    continue;
                }

                const std::string& piece = std::get<std::string>(component);
                std::filesystem::path rel = std::filesystem::path(piece);
                if (result.empty()) result = rel;
                else result /= rel;
            }

            return result.lexically_normal();
        }

        static std::string resolvePathString(const std::vector<PathComponent>& components) {
            return resolvePath(components).string();
        }

        static std::filesystem::path pathInKnownFolder(KnownFolder folder, const std::string& relativePath = "") {
            std::vector<PathComponent> components;
            components.push_back(folder);
            if (!relativePath.empty()) components.push_back(relativePath);
            return resolvePath(components);
        }

        static std::string pathInKnownFolderString(KnownFolder folder, const std::string& relativePath = "") {
            return pathInKnownFolder(folder, relativePath).string();
        }

        static std::string resolveInitialDir(const std::string& initialDir, const std::vector<PathComponent>& initialPath = {}) {
            if (!initialPath.empty()) return resolvePathString(initialPath);
            if (!initialDir.empty()) return initialDir;
            return "";
        }

        static File FromPath(std::initializer_list<PathComponent> pathComponents) {
            return File(pathComponents);
        }

        static File FromKnownFolder(KnownFolder folder, const std::string& relativePath = "") {
            if (relativePath.empty()) return File({ .path = { folder } });
            return File({ .path = { folder, relativePath } });
        }

        static File FromRoamingAppData(const std::string& relativePath = "") { return FromKnownFolder(KnownFolder::RoamingAppData, relativePath); }
        static File FromLocalAppData(const std::string& relativePath = "") { return FromKnownFolder(KnownFolder::LocalAppData, relativePath); }
        static File FromCommonAppData(const std::string& relativePath = "") { return FromKnownFolder(KnownFolder::CommonAppData, relativePath); }

        static std::string shellQuote(const std::string& value) {
            std::string out = "'";
            for (char c : value) {
                if (c == '\'') out += "'\\''";
                else out += c;
            }
            out += "'";
            return out;
        }

        static bool commandExists(const char* command) {
            std::string test = "command -v ";
            test += command;
            test += " >/dev/null 2>&1";
            return std::system(test.c_str()) == 0;
        }

        static bool runCommandCapture(const std::string& command, std::string& out) {
            out.clear();
            FILE* pipe = popen(command.c_str(), "r");
            if (!pipe) return false;

            std::array<char, 512> buffer{};
            while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
                out += buffer.data();
            }

            int status = pclose(pipe);
            while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
            return status == 0 && !out.empty();
        }

        static File Open(std::string title = "Open File", const char* filter = "All Files\0*.*\0", std::string initialDir = "") {
            File file;
            file.open(title, filter, initialDir);
            return file;
        }

        static File SaveAs(std::string title = "Save File", const char* filter = "All Files\0*.*\0", std::string initialDir = "") {
            File file;
            file.saveAs(title, filter, initialDir);
            return file;
        }

        bool open(std::string title = "Open File", const char* filter = "All Files\0*.*\0", std::string initialDir = "", const std::vector<PathComponent>& initialPath = {}) {
            (void)filter;
            initialDir = resolveInitialDir(initialDir, initialPath);
            if (initialDir.empty()) initialDir = currentDir();

            std::string selected;
            if (openDialog(selected, title, filter, initialDir, "")) {
                path = selected;
                valid = true;
                refresh();
                return true;
            }
            return false;
        }

        bool openAtCurrentDir(std::string title = "Open File", const char* filter = "All Files\0*.*\0") {
            return open(title, filter, currentDir());
        }

        bool saveAs(std::string title = "Save File", const char* filter = "All Files\0*.*\0", std::string initialDir = "", const std::vector<PathComponent>& initialPath = {}) {
            (void)filter;
            initialDir = resolveInitialDir(initialDir, initialPath);
            if (initialDir.empty()) initialDir = currentDir();

            std::string selected;
            if (saveDialog(selected, title, filter, initialDir, name)) {
                path = selected;
                valid = true;
                refresh();
                return true;
            }
            return false;
        }

        bool saveAsAtCurrentDir(std::string title = "Save File", const char* filter = "All Files\0*.*\0") {
            return saveAs(title, filter, currentDir());
        }

        bool selectFolder(std::string title = "Select Folder", std::string initialDir = "") {
            std::string selected;
            if (pickFolderDialog(selected, title, initialDir)) {
                path = selected;
                valid = true;
                refresh();
                return true;
            }
            return false;
        }

        bool save() {
            if (!valid) return saveAs();
            refresh();
            return true;
        }

        void refresh() {
            resetInfo();
            if (!valid) return;

            pathname = path.string();
            name = path.filename().string();
            ext = path.extension().string();

            exists = std::filesystem::exists(path);
            if (!exists) return;

            isDirectory = std::filesystem::is_directory(path);
            if (!isDirectory) size = std::filesystem::file_size(path);

            std::error_code ec;
            auto ftime = std::filesystem::last_write_time(path, ec);
            if (!ec) {
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
                );
                modified = sctp;
                created = sctp;
                accessed = sctp;
            }

            mime = readMimeType();
            if (!isDirectory) ingestBuffer();
        }

        void resetInfo() {
            exists = false;
            isDirectory = false;
            buffered = false;
            tooLargeToBuffer = false;
            size = 0;
            created = {};
            accessed = {};
            modified = {};
            mime = "";
            ext = "";
            name = "";
            pathname = "";
            buffer.clear();
            chars = nullptr;
            bytes = nullptr;
            data = nullptr;
        }

        void ingestBuffer() {
            if (size > maxBufferSize) {
                tooLargeToBuffer = true;
                return;
            }

            std::ifstream stream(path, std::ios::binary);
            if (!stream) return;

            buffer.resize(static_cast<size_t>(size));
            if (size > 0) stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));

            buffered = true;
            bytes = buffer.data();
            chars = reinterpret_cast<char*>(buffer.data());
            data = static_cast<void*>(buffer.data());
        }

        std::string readMimeType() {
            return fallbackMimeType();
        }

        std::string fallbackMimeType() const {
            std::string e = ext;
            for (char& c : e) c = static_cast<char>(tolower(c));

            if (e == ".step" || e == ".stp") return "model/step";
            if (e == ".stl") return "model/stl";
            if (e == ".obj") return "model/obj";
            if (e == ".txt") return "text/plain";
            if (e == ".json") return "application/json";
            if (e == ".csv") return "text/csv";
            if (e == ".png") return "image/png";
            if (e == ".jpg" || e == ".jpeg") return "image/jpeg";

            return "application/octet-stream";
        }

        bool fileExists() const { return exists; }
        bool hasPath() const { return valid && !path.empty(); }
        std::string string() const { return path.string(); }
        std::string filename() const { return path.filename().string(); }
        std::string extension() const { return path.extension().string(); }

        std::string currentDir() const {
            if (!valid || path.empty()) return "";
            if (isDirectory) return path.string();
            std::filesystem::path parent = path.parent_path();
            if (parent.empty()) return "";
            return parent.string();
        }

        bool ensureParentDirectoryExists() const {
            if (!valid || path.empty()) return false;
            std::filesystem::path parent = path.parent_path();
            if (parent.empty()) return true;
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            return !ec;
        }

        bool readText(std::string& out) const {
            out.clear();
            if (!valid || path.empty()) return false;
            if (!std::filesystem::exists(path)) return false;
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return false;
            out.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
            return true;
        }

        bool writeText(const std::string& content) const {
            if (!valid || path.empty()) return false;
            if (!ensureParentDirectoryExists()) return false;
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream) return false;
            stream << content;
            return stream.good();
        }

        static bool openDialog(std::string& out, std::string title, const char* filter, std::string initialDir = "", std::string initialFileName = "") {
            (void)filter;
            (void)initialFileName;
            if (!commandExists("zenity")) return false;

            std::string command = "zenity --file-selection --title=" + shellQuote(title);
            if (!initialDir.empty()) command += " --filename=" + shellQuote((std::filesystem::path(initialDir) / "").string());
            return runCommandCapture(command, out);
        }

        static bool saveDialog(std::string& out, std::string title, const char* filter, std::string initialDir = "", std::string initialFileName = "") {
            (void)filter;
            if (!commandExists("zenity")) return false;

            std::filesystem::path initial = initialDir.empty() ? std::filesystem::path(initialFileName) : std::filesystem::path(initialDir) / initialFileName;
            std::string command = "zenity --file-selection --save --confirm-overwrite --title=" + shellQuote(title);
            if (!initial.empty()) command += " --filename=" + shellQuote(initial.string());
            return runCommandCapture(command, out);
        }

        static bool pickFolderDialog(std::string& out, std::string title, std::string initialDir = "") {
            if (!commandExists("zenity")) return false;

            std::string command = "zenity --file-selection --directory --title=" + shellQuote(title);
            if (!initialDir.empty()) command += " --filename=" + shellQuote((std::filesystem::path(initialDir) / "").string());
            return runCommandCapture(command, out);
        }

        operator bool() const { return valid; }
    };
}
