module;

#include <string>
#include <filesystem>
#include <vector>
#include <fstream>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <cctype>

#include <windows.h>
#include <commdlg.h>

export module Rev.OS.File;

export namespace Rev::OS {

    struct File {

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
        };

        // Create
        File() {}

        File(ConstructParams p) {
            this->path = std::filesystem::path(p.pathname);
            this->valid = true;
            this->refresh();
        }

        // Static constructors
        //--------------------------------------------------

        static File Open(
            std::string title = "Open File",
            const char* filter = "All Files\0*.*\0"
        ) {
            File file;
            file.open(title, filter);
            return file;
        }

        static File SaveAs(
            std::string title = "Save File",
            const char* filter = "All Files\0*.*\0"
        ) {
            File file;
            file.saveAs(title, filter);
            return file;
        }

        // Dialogs
        //--------------------------------------------------

        bool open(
            std::string title = "Open File",
            const char* filter = "All Files\0*.*\0"
        ) {
            std::string selected;

            if (!openDialog(selected, title, filter)) {
                return false;
            }

            path = selected;
            valid = true;

            refresh();

            return true;
        }

        bool saveAs(
            std::string title = "Save File",
            const char* filter = "All Files\0*.*\0"
        ) {
            std::string selected;

            if (!saveDialog(selected, title, filter)) {
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

        // Refresh / ingest
        //--------------------------------------------------

        static std::chrono::system_clock::time_point fileTimeToChrono(FILETIME ft) {

            ULARGE_INTEGER ull;
            ull.LowPart = ft.dwLowDateTime;
            ull.HighPart = ft.dwHighDateTime;

            // FILETIME is 100ns intervals since 1601-01-01.
            // Unix/system_clock epoch is 1970-01-01.
            constexpr unsigned long long windowsToUnixEpoch100ns = 116444736000000000ULL;

            if (ull.QuadPart < windowsToUnixEpoch100ns) {
                return {};
            }

            unsigned long long unix100ns = ull.QuadPart - windowsToUnixEpoch100ns;

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

        std::string string() const {
            return path.string();
        }

        std::string filename() const {
            return path.filename().string();
        }

        std::string extension() const {
            return path.extension().string();
        }

        operator bool() const {
            return valid;
        }

        // Win32 implementation
        //--------------------------------------------------

        static bool openDialog(
            std::string& out,
            std::string title,
            const char* filter
        ) {
            char buffer[MAX_PATH] = { 0 };

            OPENFILENAMEA ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = nullptr;
            ofn.lpstrFile = buffer;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrTitle = title.c_str();
            ofn.lpstrFilter = filter;
            ofn.nFilterIndex = 1;
            ofn.Flags =
                OFN_PATHMUSTEXIST |
                OFN_FILEMUSTEXIST |
                OFN_NOCHANGEDIR;

            if (!GetOpenFileNameA(&ofn)) {
                return false;
            }

            out = buffer;
            return true;
        }

        static bool saveDialog(
            std::string& out,
            std::string title,
            const char* filter
        ) {
            char buffer[MAX_PATH] = { 0 };

            OPENFILENAMEA ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = nullptr;
            ofn.lpstrFile = buffer;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrTitle = title.c_str();
            ofn.lpstrFilter = filter;
            ofn.nFilterIndex = 1;
            ofn.Flags =
                OFN_PATHMUSTEXIST |
                OFN_OVERWRITEPROMPT |
                OFN_NOCHANGEDIR;

            if (!GetSaveFileNameA(&ofn)) {
                return false;
            }

            out = buffer;
            return true;
        }
    };
}