module;

#include <array>
#include <cstdio>
#include <cstdlib>
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
    };
}
