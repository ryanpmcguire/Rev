module;

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

export module Rev.OS.Dialog;

export namespace Rev::OS {

    enum class DialogResult {
        None,
        Ok,
        Cancel,
        Yes,
        No
    };

    enum class UnsavedChangesResult {
        Save,
        Discard,
        Cancel
    };

    // Zenity-only -- kdialog is a KDE/Plasma tool that can be present (as a
    // transitive dependency of something else) without a working Plasma/D-Bus
    // session behind it, in which case invoking it can hang or misbehave
    // instead of cleanly failing. commandExists() only checks the binary
    // exists, not that it actually works outside a KDE session, so on
    // GTK-based desktops (XFCE, GNOME, Cinnamon, ...) it's not a safe first
    // choice. zenity is the reliable baseline there.
    struct Dialog {

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

        static bool runZenity(const std::string& kind, const std::string& title, const std::string& message) {
            if (!commandExists("zenity")) return false;
            std::string command = "zenity --" + kind + " --title=" + shellQuote(title) + " --text=" + shellQuote(message) + " >/dev/null 2>&1";
            return std::system(command.c_str()) == 0;
        }

        static DialogResult Info(
            const std::string& title,
            const std::string& message,
            void* owner = nullptr
        ) {
            (void)owner;
            if (runZenity("info", title, message)) return DialogResult::Ok;
            std::fprintf(stderr, "[Info] %s: %s\n", title.c_str(), message.c_str());
            return DialogResult::Ok;
        }

        static DialogResult Warning(
            const std::string& title,
            const std::string& message,
            void* owner = nullptr
        ) {
            (void)owner;
            if (runZenity("warning", title, message)) return DialogResult::Ok;
            std::fprintf(stderr, "[Warning] %s: %s\n", title.c_str(), message.c_str());
            return DialogResult::Ok;
        }

        static DialogResult Error(
            const std::string& title,
            const std::string& message,
            void* owner = nullptr
        ) {
            (void)owner;
            if (runZenity("error", title, message)) return DialogResult::Ok;
            std::fprintf(stderr, "[Error] %s: %s\n", title.c_str(), message.c_str());
            return DialogResult::Ok;
        }

        static DialogResult Confirm(
            const std::string& title,
            const std::string& message,
            void* owner = nullptr
        ) {
            (void)owner;
            if (commandExists("zenity")) {
                std::string command = "zenity --question --title=" + shellQuote(title) + " --text=" + shellQuote(message) + " >/dev/null 2>&1";
                return std::system(command.c_str()) == 0 ? DialogResult::Yes : DialogResult::No;
            }
            std::fprintf(stderr, "[Confirm] %s: %s\n", title.c_str(), message.c_str());
            return DialogResult::No;
        }

        static UnsavedChangesResult UnsavedChanges(
            const std::string& itemName = "Untitled Project",
            void* owner = nullptr
        ) {
            (void)owner;

            if (commandExists("zenity")) {
                std::string output;
                std::string command =
                    "zenity --question"
                    " --title=" + shellQuote("Unsaved Changes") +
                    " --text=" + shellQuote("Save changes to \"" + itemName + "\" before closing?") +
                    " --ok-label=" + shellQuote("Save") +
                    " --cancel-label=" + shellQuote("Cancel") +
                    " --extra-button=" + shellQuote("Discard") +
                    " 2>/dev/null";

                FILE* pipe = popen(command.c_str(), "r");
                if (pipe) {
                    std::array<char, 128> buffer{};
                    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
                        output += buffer.data();
                    }

                    int status = pclose(pipe);
                    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();

                    if (output == "Discard") return UnsavedChangesResult::Discard;
                    if (status == 0) return UnsavedChangesResult::Save;
                    return UnsavedChangesResult::Cancel;
                }
            }

            std::fprintf(stderr, "[UnsavedChanges] %s\n", itemName.c_str());
            return UnsavedChangesResult::Cancel;
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

        static std::string usableInitialDir(const std::string& initialDir) {
            if (!initialDir.empty()) {
                std::error_code ec;
                if (std::filesystem::is_directory(initialDir, ec)) return initialDir;
            }
            if (const char* home = std::getenv("HOME")) {
                if (*home) return home;
            }
            return std::filesystem::current_path().string();
        }

        // Native file-open dialog via zenity. `filter` is accepted for
        // signature parity with the Windows OPENFILENAME convention but is
        // not applied -- zenity's --file-filter support is inconsistent
        // enough across desktop environments not to rely on it.
        static bool OpenFile(
            std::string& outPath,
            const std::string& title,
            const char* filter = "All Files\0*.*\0",
            const std::string& initialDir = "",
            void* owner = nullptr
        ) {
            (void)filter;
            (void)owner;

            if (!commandExists("zenity")) {
                std::fprintf(stderr, "[OpenFile] %s: zenity not found\n", title.c_str());
                return false;
            }

            std::string dir = usableInitialDir(initialDir);
            std::string command = "zenity --file-selection --title=" + shellQuote(title) +
                " --filename=" + shellQuote((std::filesystem::path(dir) / "").string());
            return runCommandCapture(command, outPath);
        }

        // Native folder-picker dialog via zenity.
        static bool PickFolder(
            std::string& outPath,
            const std::string& title,
            void* owner = nullptr
        ) {
            (void)owner;

            if (!commandExists("zenity")) {
                std::fprintf(stderr, "[PickFolder] %s: zenity not found\n", title.c_str());
                return false;
            }

            std::string dir = usableInitialDir("");
            std::string command = "zenity --file-selection --directory --title=" + shellQuote(title) +
                " --filename=" + shellQuote((std::filesystem::path(dir) / "").string());
            return runCommandCapture(command, outPath);
        }
    };
}
