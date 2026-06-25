module;

#include <string>
#include <filesystem>
#include <vector>
#include <fstream>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <cctype>
#include <system_error>
#include <variant>
#include <initializer_list>

#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <objbase.h>

export module Rev.OS.File;

export namespace Rev::OS {

    struct File {

        // Windows Shell "Known Folders" (SHGetKnownFolderPath / FOLDERID_*).
        // Names follow Microsoft's terminology.
        enum class KnownFolder {

            // Per-user, roams with the profile (%APPDATA%).
            RoamingAppData,

            // Per-user, stays on this machine (%LOCALAPPDATA%).
            LocalAppData,

            // Machine-wide application data (%PROGRAMDATA%).
            CommonAppData,

            UserProfile,
            Documents,
            Desktop,
            Downloads,

            ProgramFiles,
            ProgramFilesX86,
        };

        // One step in a logical path: a Known Folder root, or a relative/absolute
        // path fragment (may contain separators, e.g. "CAM/persist.json").
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

            // Legacy: single absolute or relative path (used when path is empty).
            std::string pathname = "";

            // Composed path, applied in order, e.g.:
            // { KnownFolder::RoamingAppData, "CAM", "persist.json" }
            // { KnownFolder::Desktop, "./FolderOnDesktop", "Part.step" }
            std::vector<PathComponent> path = {};
        };

        // Create
        //--------------------------------------------------

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

        // String conversion helpers
        //--------------------------------------------------

        static std::wstring widen(
            const std::string& value
        ) {
            if (value.empty()) { return L""; }

            int size = MultiByteToWideChar(
                CP_UTF8,
                0,
                value.c_str(),
                -1,
                nullptr,
                0
            );

            if (size <= 0) { return L""; }

            std::wstring out;
            out.resize(size - 1);

            MultiByteToWideChar(
                CP_UTF8,
                0,
                value.c_str(),
                -1,
                out.data(),
                size
            );

            return out;
        }

        static std::string narrow(
            const wchar_t* value
        ) {
            if (!value) { return ""; }

            int size = WideCharToMultiByte(
                CP_UTF8,
                0,
                value,
                -1,
                nullptr,
                0,
                nullptr,
                nullptr
            );

            if (size <= 0) { return ""; }

            std::string out;
            out.resize(size - 1);

            WideCharToMultiByte(
                CP_UTF8,
                0,
                value,
                -1,
                out.data(),
                size,
                nullptr,
                nullptr
            );

            return out;
        }

        // Known folders
        //--------------------------------------------------

        static const KNOWNFOLDERID* knownFolderId(
            KnownFolder folder
        ) {
            switch (folder) {

                case KnownFolder::RoamingAppData:
                    return &FOLDERID_RoamingAppData;

                case KnownFolder::LocalAppData:
                    return &FOLDERID_LocalAppData;

                case KnownFolder::CommonAppData:
                    return &FOLDERID_ProgramData;

                case KnownFolder::UserProfile:
                    return &FOLDERID_Profile;

                case KnownFolder::Documents:
                    return &FOLDERID_Documents;

                case KnownFolder::Desktop:
                    return &FOLDERID_Desktop;

                case KnownFolder::Downloads:
                    return &FOLDERID_Downloads;

                case KnownFolder::ProgramFiles:
                    return &FOLDERID_ProgramFiles;

                case KnownFolder::ProgramFilesX86:
                    return &FOLDERID_ProgramFilesX86;

                default:
                    return nullptr;
            }
        }

        static std::filesystem::path knownFolderRoot(
            KnownFolder folder
        ) {
            const KNOWNFOLDERID* id = knownFolderId(folder);

            if (!id) {
                return {};
            }

            PWSTR wide = nullptr;

            HRESULT hr = SHGetKnownFolderPath(
                *id,
                KF_FLAG_DEFAULT,
                nullptr,
                &wide
            );

            if (FAILED(hr) || !wide) {
                return {};
            }

            std::filesystem::path root = wide;
            CoTaskMemFree(wide);

            return root;
        }

        // Compose a filesystem path from ordered components.
        // Typically the first component is a KnownFolder; later strings append.
        // Only one KnownFolder is allowed (must be the first component).
        static std::filesystem::path resolvePath(
            const std::vector<PathComponent>& components
        ) {
            if (components.empty()) {
                return {};
            }

            std::filesystem::path result;
            bool hasKnownFolderRoot = false;

            for (const PathComponent& component : components) {

                if (std::holds_alternative<KnownFolder>(component)) {

                    if (hasKnownFolderRoot) {
                        return {};
                    }

                    KnownFolder folder = std::get<KnownFolder>(component);

                    std::filesystem::path root = knownFolderRoot(folder);

                    if (root.empty()) {
                        return {};
                    }

                    result = root;
                    hasKnownFolderRoot = true;
                    continue;
                }

                const std::string& piece = std::get<std::string>(component);
                std::filesystem::path rel = std::filesystem::path(piece);

                if (result.empty()) {
                    result = rel;
                    continue;
                }

                result /= rel;
            }

            return result.lexically_normal();
        }

        static std::string resolvePathString(
            const std::vector<PathComponent>& components
        ) {
            return resolvePath(components).string();
        }

        static std::filesystem::path pathInKnownFolder(
            KnownFolder folder,
            const std::string& relativePath = ""
        ) {
            std::vector<PathComponent> components;
            components.push_back(folder);

            if (!relativePath.empty()) {
                components.push_back(relativePath);
            }

            return resolvePath(components);
        }

        static std::string pathInKnownFolderString(
            KnownFolder folder,
            const std::string& relativePath = ""
        ) {
            return pathInKnownFolder(folder, relativePath).string();
        }

        static std::string resolveInitialDir(
            const std::string& initialDir,
            const std::vector<PathComponent>& initialPath = {}
        ) {
            if (!initialPath.empty()) {
                return resolvePathString(initialPath);
            }

            if (!initialDir.empty()) {
                return initialDir;
            }

            return "";
        }

        static File FromPath(
            std::initializer_list<PathComponent> pathComponents
        ) {
            return File(pathComponents);
        }

        static File FromKnownFolder(
            KnownFolder folder,
            const std::string& relativePath = ""
        ) {
            if (relativePath.empty()) {
                return File({ .path = { folder } });
            }

            return File({ .path = { folder, relativePath } });
        }

        static File FromRoamingAppData(const std::string& relativePath = "") {
            return FromKnownFolder(KnownFolder::RoamingAppData, relativePath);
        }

        static File FromLocalAppData(const std::string& relativePath = "") {
            return FromKnownFolder(KnownFolder::LocalAppData, relativePath);
        }

        static File FromCommonAppData(const std::string& relativePath = "") {
            return FromKnownFolder(KnownFolder::CommonAppData, relativePath);
        }

        // Static constructors
        //--------------------------------------------------

        static File Open(
            std::string title = "Open File",
            const char* filter = "All Files\0*.*\0",
            std::string initialDir = ""
        ) {
            File file;
            file.open(title, filter, initialDir);
            return file;
        }

        static File SaveAs(
            std::string title = "Save File",
            const char* filter = "All Files\0*.*\0",
            std::string initialDir = ""
        ) {
            File file;
            file.saveAs(title, filter, initialDir);
            return file;
        }

        // Dialogs
        //--------------------------------------------------

        bool open(
            std::string title = "Open File",
            const char* filter = "All Files\0*.*\0",
            std::string initialDir = "",
            const std::vector<PathComponent>& initialPath = {}
        ) {
            std::string selected;

            initialDir = resolveInitialDir(
                initialDir,
                initialPath
            );

            if (initialDir.empty()) {
                initialDir = currentDir();
            }

            std::string initialFileName = "";

            if (valid && exists && !isDirectory && !name.empty()) {
                initialFileName = name;
            }

            if (!openDialog(
                selected,
                title,
                filter,
                initialDir,
                initialFileName
            )) {
                return false;
            }

            path = selected;
            valid = true;

            refresh();

            return true;
        }

        bool openAtCurrentDir(
            std::string title = "Open File",
            const char* filter = "All Files\0*.*\0"
        ) {
            return open(
                title,
                filter,
                currentDir()
            );
        }

        bool saveAs(
            std::string title = "Save File",
            const char* filter = "All Files\0*.*\0",
            std::string initialDir = "",
            const std::vector<PathComponent>& initialPath = {}
        ) {
            std::string selected;

            initialDir = resolveInitialDir(
                initialDir,
                initialPath
            );

            if (initialDir.empty()) {
                initialDir = currentDir();
            }

            std::string initialFileName = "";

            if (valid && !isDirectory && !name.empty()) {
                initialFileName = name;
            }

            if (!saveDialog(
                selected,
                title,
                filter,
                initialDir,
                initialFileName
            )) {
                return false;
            }

            path = selected;
            valid = true;

            refresh();

            return true;
        }

        bool saveAsAtCurrentDir(
            std::string title = "Save File",
            const char* filter = "All Files\0*.*\0"
        ) {
            return saveAs(
                title,
                filter,
                currentDir()
            );
        }

        bool selectFolder(
            std::string title = "Select Folder",
            std::string initialDir = ""
        ) {
            std::string selected;

            if (!pickFolderDialog(
                selected,
                title,
                initialDir
            )) {
                return false;
            }

            path = selected;
            valid = true;

            refresh();

            return true;
        }

        bool save() {

            if (!valid) {
                return saveAs();
            }

            refresh();

            return true;
        }

        bool reveal() const {

            if (!valid || path.empty()) {
                return false;
            }

            return revealPath(path);
        }

        static bool Reveal(const std::string& pathname) {
            return revealPath(std::filesystem::path(pathname));
        }

        static bool revealPath(const std::filesystem::path& target) {

            if (target.empty()) {
                return false;
            }

            std::error_code error;

            if (std::filesystem::is_regular_file(target, error)) {
                std::wstring argument = L"/select,\"" + target.wstring() + L"\"";

                HINSTANCE result = ShellExecuteW(
                    nullptr,
                    L"open",
                    L"explorer.exe",
                    argument.c_str(),
                    nullptr,
                    SW_SHOWNORMAL
                );

                return reinterpret_cast<intptr_t>(result) > 32;
            }

            std::filesystem::path folder = target;

            if (!std::filesystem::is_directory(folder, error)) {
                folder = target.parent_path();
            }

            if (folder.empty() || !std::filesystem::is_directory(folder, error)) {
                return false;
            }

            HINSTANCE result = ShellExecuteW(
                nullptr,
                L"open",
                folder.wstring().c_str(),
                nullptr,
                nullptr,
                SW_SHOWNORMAL
            );

            return reinterpret_cast<intptr_t>(result) > 32;
        }

        // Refresh / ingest
        //--------------------------------------------------

        static std::chrono::system_clock::time_point fileTimeToChrono(
            FILETIME ft
        ) {
            ULARGE_INTEGER ull;
            ull.LowPart = ft.dwLowDateTime;
            ull.HighPart = ft.dwHighDateTime;

            constexpr unsigned long long windowsToUnixEpoch100ns =
                116444736000000000ULL;

            if (ull.QuadPart < windowsToUnixEpoch100ns) {
                return {};
            }

            unsigned long long unix100ns =
                ull.QuadPart - windowsToUnixEpoch100ns;

            return std::chrono::system_clock::time_point(
                std::chrono::duration_cast<std::chrono::system_clock::duration>(
                    std::chrono::nanoseconds(unix100ns * 100)
                )
            );
        }

        void refresh() {

            resetInfo();

            if (!valid) {
                return;
            }

            pathname = path.string();
            name = path.filename().string();
            ext = path.extension().string();

            exists = std::filesystem::exists(path);

            if (!exists) {
                return;
            }

            isDirectory = std::filesystem::is_directory(path);

            if (!isDirectory) {
                size = std::filesystem::file_size(path);
            }

            readWin32Times();
            mime = readMimeType();

            if (!isDirectory) {
                ingestBuffer();
            }
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

            std::ifstream stream(
                path,
                std::ios::binary
            );

            if (!stream) {
                return;
            }

            buffer.resize(
                static_cast<size_t>(size)
            );

            if (size > 0) {
                stream.read(
                    reinterpret_cast<char*>(buffer.data()),
                    static_cast<std::streamsize>(size)
                );
            }

            buffered = true;

            bytes = buffer.data();
            chars = reinterpret_cast<char*>(buffer.data());
            data = static_cast<void*>(buffer.data());
        }

        void readWin32Times() {

            WIN32_FILE_ATTRIBUTE_DATA info = {};

            if (!GetFileAttributesExA(
                pathname.c_str(),
                GetFileExInfoStandard,
                &info
            )) {
                return;
            }

            created = fileTimeToChrono(info.ftCreationTime);
            accessed = fileTimeToChrono(info.ftLastAccessTime);
            modified = fileTimeToChrono(info.ftLastWriteTime);
        }

        std::string readMimeType() {

            if (ext.empty()) {
                return "application/octet-stream";
            }

            HKEY key = nullptr;

            LONG result = RegOpenKeyExA(
                HKEY_CLASSES_ROOT,
                ext.c_str(),
                0,
                KEY_READ,
                &key
            );

            if (result != ERROR_SUCCESS) {
                return fallbackMimeType();
            }

            char value[256] = {};
            DWORD valueSize = sizeof(value);

            result = RegQueryValueExA(
                key,
                "Content Type",
                nullptr,
                nullptr,
                reinterpret_cast<LPBYTE>(value),
                &valueSize
            );

            RegCloseKey(key);

            if (result != ERROR_SUCCESS) {
                return fallbackMimeType();
            }

            return std::string(value);
        }

        std::string fallbackMimeType() const {

            std::string e = ext;

            for (char& c : e) {
                c = static_cast<char>(tolower(c));
            }

            if (e == ".step" || e == ".stp") { return "model/step"; }
            if (e == ".stl") { return "model/stl"; }
            if (e == ".obj") { return "model/obj"; }
            if (e == ".txt") { return "text/plain"; }
            if (e == ".json") { return "application/json"; }
            if (e == ".csv") { return "text/csv"; }
            if (e == ".png") { return "image/png"; }
            if (e == ".jpg" || e == ".jpeg") { return "image/jpeg"; }

            return "application/octet-stream";
        }

        // Query
        //--------------------------------------------------

        bool fileExists() const {
            return exists;
        }

        bool hasPath() const {
            return valid && !path.empty();
        }

        std::string string() const {
            return path.string();
        }

        std::string filename() const {
            return path.filename().string();
        }

        std::string extension() const {
            return path.extension().string();
        }

        std::string currentDir() const {

            if (!valid || path.empty()) {
                return "";
            }

            if (isDirectory) {
                return path.string();
            }

            std::filesystem::path parent = path.parent_path();

            if (parent.empty()) {
                return "";
            }

            return parent.string();
        }

        // Ensure parent directories exist (e.g. before writing persist.json).
        bool ensureParentDirectoryExists() const {

            if (!valid || path.empty()) {
                return false;
            }

            std::filesystem::path parent = path.parent_path();

            if (parent.empty()) {
                return true;
            }

            std::error_code ec;
            std::filesystem::create_directories(parent, ec);

            return !ec;
        }

        // Read entire file as UTF-8 text (file must exist).
        bool readText(
            std::string& out
        ) const {
            out.clear();

            if (!valid || path.empty()) {
                return false;
            }

            if (!std::filesystem::exists(path)) {
                return false;
            }

            std::ifstream stream(
                path,
                std::ios::binary
            );

            if (!stream) {
                return false;
            }

            out.assign(
                std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>()
            );

            return true;
        }

        // Write UTF-8 text, creating the parent directory tree if needed.
        bool writeText(
            const std::string& content
        ) const {
            if (!valid || path.empty()) {
                return false;
            }

            if (!ensureParentDirectoryExists()) {
                return false;
            }

            std::ofstream stream(
                path,
                std::ios::binary | std::ios::trunc
            );

            if (!stream) {
                return false;
            }

            stream << content;

            return stream.good();
        }

        operator bool() const {
            return valid;
        }

        // Filter parsing
        //--------------------------------------------------

        static void parseFilter(
            const char* filter,
            std::vector<std::wstring>& names,
            std::vector<std::wstring>& patterns,
            std::vector<COMDLG_FILTERSPEC>& specs
        ) {
            names.clear();
            patterns.clear();
            specs.clear();

            if (!filter) { return; }

            const char* p = filter;

            while (*p) {

                std::string label = p;
                p += label.size() + 1;

                if (!*p) { break; }

                std::string pattern = p;
                p += pattern.size() + 1;

                names.push_back(widen(label));
                patterns.push_back(widen(pattern));
            }

            for (size_t i = 0; i < names.size(); i++) {
                specs.push_back({
                    names[i].c_str(),
                    patterns[i].c_str()
                });
            }
        }

        // COM dialog helpers
        //--------------------------------------------------

        struct ComScope {

            bool initialized = false;

            ComScope() {

                HRESULT hr = CoInitializeEx(
                    nullptr,
                    COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE
                );

                initialized = SUCCEEDED(hr);
            }

            ~ComScope() {

                if (initialized) {
                    CoUninitialize();
                }
            }
        };

        static void applyDialogBasics(
            IFileDialog* dialog,
            const std::string& title,
            const char* filter,
            std::vector<std::wstring>& filterNames,
            std::vector<std::wstring>& filterPatterns,
            std::vector<COMDLG_FILTERSPEC>& filterSpecs
        ) {
            if (!dialog) { return; }

            std::wstring wTitle = widen(title);

            if (!wTitle.empty()) {
                dialog->SetTitle(wTitle.c_str());
            }

            DWORD options = 0;

            if (SUCCEEDED(dialog->GetOptions(&options))) {
                dialog->SetOptions(
                    options |
                    FOS_FORCEFILESYSTEM |
                    FOS_PATHMUSTEXIST
                );
            }

            parseFilter(
                filter,
                filterNames,
                filterPatterns,
                filterSpecs
            );

            if (!filterSpecs.empty()) {
                dialog->SetFileTypes(
                    static_cast<UINT>(filterSpecs.size()),
                    filterSpecs.data()
                );

                dialog->SetFileTypeIndex(1);
            }
        }

        static void applyInitialFolder(
            IFileDialog* dialog,
            const std::string& initialDir
        ) {
            if (!dialog) { return; }
            if (initialDir.empty()) { return; }

            std::filesystem::path dir = std::filesystem::path(initialDir);

            if (!std::filesystem::exists(dir)) {
                return;
            }

            if (!std::filesystem::is_directory(dir)) {
                dir = dir.parent_path();
            }

            if (dir.empty()) { return; }

            std::wstring wInitialDir = dir.make_preferred().wstring();

            IShellItem* folder = nullptr;

            HRESULT hr = SHCreateItemFromParsingName(
                wInitialDir.c_str(),
                nullptr,
                IID_PPV_ARGS(&folder)
            );

            if (FAILED(hr) || !folder) {
                return;
            }

            // SetDefaultFolder = fallback/default.
            dialog->SetDefaultFolder(folder);

            // SetFolder = force open here.
            dialog->SetFolder(folder);

            folder->Release();
        }

        static void applyInitialFileName(
            IFileDialog* dialog,
            const std::string& initialFileName
        ) {
            if (!dialog) { return; }
            if (initialFileName.empty()) { return; }

            std::wstring wInitialFileName = widen(initialFileName);

            if (!wInitialFileName.empty()) {
                dialog->SetFileName(wInitialFileName.c_str());
            }
        }

        static bool readDialogResult(
            IFileDialog* dialog,
            std::string& out
        ) {
            if (!dialog) { return false; }

            IShellItem* result = nullptr;

            HRESULT hr = dialog->GetResult(&result);

            if (FAILED(hr) || !result) {
                return false;
            }

            PWSTR selected = nullptr;

            hr = result->GetDisplayName(
                SIGDN_FILESYSPATH,
                &selected
            );

            if (SUCCEEDED(hr) && selected) {
                out = narrow(selected);
                CoTaskMemFree(selected);
            }

            result->Release();

            return !out.empty();
        }

        // Windows implementation
        //--------------------------------------------------

        static bool openDialog(
            std::string& out,
            std::string title,
            const char* filter,
            std::string initialDir = "",
            std::string initialFileName = ""
        ) {
            out = "";

            ComScope com;

            IFileOpenDialog* dialog = nullptr;

            HRESULT hr = CoCreateInstance(
                CLSID_FileOpenDialog,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&dialog)
            );

            if (FAILED(hr) || !dialog) {
                return false;
            }

            std::vector<std::wstring> filterNames;
            std::vector<std::wstring> filterPatterns;
            std::vector<COMDLG_FILTERSPEC> filterSpecs;

            applyDialogBasics(
                dialog,
                title,
                filter,
                filterNames,
                filterPatterns,
                filterSpecs
            );

            DWORD options = 0;

            if (SUCCEEDED(dialog->GetOptions(&options))) {
                dialog->SetOptions(
                    options |
                    FOS_FORCEFILESYSTEM |
                    FOS_FILEMUSTEXIST |
                    FOS_PATHMUSTEXIST
                );
            }

            applyInitialFolder(
                dialog,
                initialDir
            );

            applyInitialFileName(
                dialog,
                initialFileName
            );

            hr = dialog->Show(nullptr);

            if (FAILED(hr)) {
                dialog->Release();
                return false;
            }

            bool ok = readDialogResult(
                dialog,
                out
            );

            dialog->Release();

            return ok;
        }

        static bool pickFolderDialog(
            std::string& out,
            std::string title,
            std::string initialDir = ""
        ) {
            out = "";

            ComScope com;

            IFileOpenDialog* dialog = nullptr;

            HRESULT hr = CoCreateInstance(
                CLSID_FileOpenDialog,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&dialog)
            );

            if (FAILED(hr) || !dialog) {
                return false;
            }

            std::vector<std::wstring> filterNames;
            std::vector<std::wstring> filterPatterns;
            std::vector<COMDLG_FILTERSPEC> filterSpecs;

            applyDialogBasics(
                dialog,
                title,
                "All Files\0*.*\0",
                filterNames,
                filterPatterns,
                filterSpecs
            );

            DWORD options = 0;

            if (SUCCEEDED(dialog->GetOptions(&options))) {
                dialog->SetOptions(
                    options |
                    FOS_FORCEFILESYSTEM |
                    FOS_PICKFOLDERS |
                    FOS_PATHMUSTEXIST
                );
            }

            applyInitialFolder(
                dialog,
                initialDir
            );

            hr = dialog->Show(nullptr);

            if (FAILED(hr)) {
                dialog->Release();
                return false;
            }

            bool ok = readDialogResult(
                dialog,
                out
            );

            dialog->Release();

            return ok;
        }

        static bool saveDialog(
            std::string& out,
            std::string title,
            const char* filter,
            std::string initialDir = "",
            std::string initialFileName = ""
        ) {
            out = "";

            ComScope com;

            IFileSaveDialog* dialog = nullptr;

            HRESULT hr = CoCreateInstance(
                CLSID_FileSaveDialog,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&dialog)
            );

            if (FAILED(hr) || !dialog) {
                return false;
            }

            std::vector<std::wstring> filterNames;
            std::vector<std::wstring> filterPatterns;
            std::vector<COMDLG_FILTERSPEC> filterSpecs;

            applyDialogBasics(
                dialog,
                title,
                filter,
                filterNames,
                filterPatterns,
                filterSpecs
            );

            applyInitialFolder(
                dialog,
                initialDir
            );

            applyInitialFileName(
                dialog,
                initialFileName
            );

            hr = dialog->Show(nullptr);

            if (FAILED(hr)) {
                dialog->Release();
                return false;
            }

            bool ok = readDialogResult(
                dialog,
                out
            );

            dialog->Release();

            return ok;
        }
    };
}
