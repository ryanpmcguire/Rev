module;

#include <string>

#include <windows.h>

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
            const std::string& message
        ) {
            int result = MessageBoxA(
                nullptr,
                message.c_str(),
                title.c_str(),
                MB_OK | MB_ICONINFORMATION
            );

            return fromMessageBoxResult(result);
        }

        static DialogResult Warning(
            const std::string& title,
            const std::string& message
        ) {
            int result = MessageBoxA(
                nullptr,
                message.c_str(),
                title.c_str(),
                MB_OK | MB_ICONWARNING
            );

            return fromMessageBoxResult(result);
        }

        static DialogResult Error(
            const std::string& title,
            const std::string& message
        ) {
            int result = MessageBoxA(
                nullptr,
                message.c_str(),
                title.c_str(),
                MB_OK | MB_ICONERROR
            );

            return fromMessageBoxResult(result);
        }

        static DialogResult Confirm(
            const std::string& title,
            const std::string& message
        ) {
            int result = MessageBoxA(
                nullptr,
                message.c_str(),
                title.c_str(),
                MB_YESNO | MB_ICONQUESTION
            );

            return fromMessageBoxResult(result);
        }

        static UnsavedChangesResult UnsavedChanges(
            const std::string& projectName = "Untitled Project"
        ) {
            std::string message =
                "Save changes to \"" +
                projectName +
                "\" before closing?";

            int result = MessageBoxA(
                nullptr,
                message.c_str(),
                "Unsaved Changes",
                MB_YESNOCANCEL | MB_ICONWARNING
            );

            if (result == IDYES) {
                return UnsavedChangesResult::Save;
            }

            if (result == IDNO) {
                return UnsavedChangesResult::Discard;
            }

            return UnsavedChangesResult::Cancel;
        }

        static DialogResult fromMessageBoxResult(
            int result
        ) {
            switch (result) {

                case IDOK: {
                    return DialogResult::Ok;
                }

                case IDCANCEL: {
                    return DialogResult::Cancel;
                }

                case IDYES: {
                    return DialogResult::Yes;
                }

                case IDNO: {
                    return DialogResult::No;
                }

                default: {
                    return DialogResult::None;
                }
            }
        }
    };
}