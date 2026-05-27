module;

#include <cstdio>
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

        static DialogResult Info(
            const std::string& title,
            const std::string& message,
            void* owner = nullptr
        ) {
            (void)owner;
            std::fprintf(stderr, "[Info] %s: %s\n", title.c_str(), message.c_str());
            return DialogResult::Ok;
        }

        static DialogResult Warning(
            const std::string& title,
            const std::string& message,
            void* owner = nullptr
        ) {
            (void)owner;
            std::fprintf(stderr, "[Warning] %s: %s\n", title.c_str(), message.c_str());
            return DialogResult::Ok;
        }

        static DialogResult Error(
            const std::string& title,
            const std::string& message,
            void* owner = nullptr
        ) {
            (void)owner;
            std::fprintf(stderr, "[Error] %s: %s\n", title.c_str(), message.c_str());
            return DialogResult::Ok;
        }

        static DialogResult Confirm(
            const std::string& title,
            const std::string& message,
            void* owner = nullptr
        ) {
            (void)owner;
            std::fprintf(stderr, "[Confirm] %s: %s\n", title.c_str(), message.c_str());
            return DialogResult::No;
        }

        static UnsavedChangesResult UnsavedChanges(
            const std::string& itemName = "Untitled Project",
            void* owner = nullptr
        ) {
            (void)owner;
            std::fprintf(stderr, "[UnsavedChanges] %s\n", itemName.c_str());
            return UnsavedChangesResult::Cancel;
        }
    };
}
