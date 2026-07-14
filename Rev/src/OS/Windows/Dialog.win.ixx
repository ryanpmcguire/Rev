module;

#include <string>

#include <windows.h>
#include <commdlg.h>

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
            MessageBoxA((HWND)owner, message.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
            return DialogResult::Ok;
        }

        static DialogResult Warning(
            const std::string& title,
            const std::string& message,
            void* owner = nullptr
        ) {
            MessageBoxA((HWND)owner, message.c_str(), title.c_str(), MB_OK | MB_ICONWARNING);
            return DialogResult::Ok;
        }

        static DialogResult Error(
            const std::string& title,
            const std::string& message,
            void* owner = nullptr
        ) {
            MessageBoxA((HWND)owner, message.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
            return DialogResult::Ok;
        }

        static DialogResult Confirm(
            const std::string& title,
            const std::string& message,
            void* owner = nullptr
        ) {
            int r = MessageBoxA((HWND)owner, message.c_str(), title.c_str(), MB_YESNO | MB_ICONQUESTION);
            return r == IDYES ? DialogResult::Yes : DialogResult::No;
        }

        static UnsavedChangesResult UnsavedChanges(
            const std::string& itemName = "Untitled Project",
            void* owner = nullptr
        ) {
            std::string message = "Save changes to \"" + itemName + "\" before closing?";
            int r = MessageBoxA((HWND)owner, message.c_str(), "Unsaved Changes",
                                 MB_YESNOCANCEL | MB_ICONWARNING);
            if (r == IDYES) return UnsavedChangesResult::Save;
            if (r == IDNO)  return UnsavedChangesResult::Discard;
            return UnsavedChangesResult::Cancel;
        }

        // Native file-open dialog. `filter` follows the OPENFILENAME convention:
        // pairs of "Description\0*.ext1;*.ext2\0" terminated by an extra \0.
        static bool OpenFile(
            std::string& outPath,
            const std::string& title,
            const char* filter = "All Files\0*.*\0",
            const std::string& initialDir = "",
            void* owner = nullptr
        ) {
            char path[MAX_PATH] = {};

            OPENFILENAMEA ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = (HWND)owner;
            ofn.lpstrFile   = path;
            ofn.nMaxFile    = MAX_PATH;
            ofn.lpstrFilter = filter;
            ofn.lpstrTitle  = title.c_str();
            ofn.lpstrInitialDir = initialDir.empty() ? nullptr : initialDir.c_str();
            ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

            if (!GetOpenFileNameA(&ofn)) return false;

            outPath = path;
            return true;
        }
    };
}
