module;

#include <string>

#include <windows.h>
#include <commctrl.h>

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

        using TaskDialogIndirectFn = HRESULT (WINAPI *)(
            const TASKDIALOGCONFIG*,
            int*,
            int*,
            BOOL*
        );

        static TaskDialogIndirectFn taskDialogIndirect() {

            static TaskDialogIndirectFn fn = nullptr;
            static bool initialized = false;

            if (!initialized) {
                initialized = true;

                HMODULE comctl = LoadLibraryW(L"comctl32.dll");

                if (comctl) {
                    fn = reinterpret_cast<TaskDialogIndirectFn>(
                        GetProcAddress(comctl, "TaskDialogIndirect")
                    );
                }
            }

            return fn;
        }

        static bool runTaskDialog(
            const TASKDIALOGCONFIG& config,
            int& pressed
        ) {
            TaskDialogIndirectFn fn = taskDialogIndirect();

            if (!fn) {
                return false;
            }

            return SUCCEEDED(fn(&config, &pressed, nullptr, nullptr));
        }

        static std::wstring widen(const std::string& text) {

            if (text.empty()) {
                return L"";
            }

            int chars = MultiByteToWideChar(
                CP_UTF8,
                0,
                text.c_str(),
                static_cast<int>(text.size()),
                nullptr,
                0
            );

            if (chars <= 0) {
                return L"";
            }

            std::wstring out(chars, L'\0');

            MultiByteToWideChar(
                CP_UTF8,
                0,
                text.c_str(),
                static_cast<int>(text.size()),
                out.data(),
                chars
            );

            return out;
        }

        static DialogResult fromTaskButton(int button) {

            switch (button) {

                case IDOK: { return DialogResult::Ok; }
                case IDCANCEL: { return DialogResult::Cancel; }
                case IDYES: { return DialogResult::Yes; }
                case IDNO: { return DialogResult::No; }

                default: { return DialogResult::None; }
            }
        }

        static DialogResult showTask(
            void* owner,
            const std::wstring& title,
            const std::wstring& instruction,
            const std::wstring& content,
            TASKDIALOG_COMMON_BUTTON_FLAGS buttons,
            int defaultButton = 0
        ) {
            HWND hwnd = static_cast<HWND>(owner);

            TASKDIALOGCONFIG config = {};
            config.cbSize = sizeof(TASKDIALOGCONFIG);
            config.hwndParent = hwnd;
            config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
            config.pszWindowTitle = title.c_str();
            config.pszMainInstruction = instruction.c_str();
            config.pszContent = content.empty() ? nullptr : content.c_str();
            config.dwCommonButtons = buttons;
            config.nDefaultButton = defaultButton;
            config.pszMainIcon = TD_WARNING_ICON;

            int pressed = 0;

            if (!runTaskDialog(config, pressed)) {
                return DialogResult::None;
            }

            return fromTaskButton(pressed);
        }

        static DialogResult Info(
            const std::string& title,
            const std::string& message,
            void* owner = nullptr
        ) {
            HWND hwnd = static_cast<HWND>(owner);
            std::wstring wTitle = widen(title);
            std::wstring wMessage = widen(message);

            TASKDIALOGCONFIG config = {};
            config.cbSize = sizeof(TASKDIALOGCONFIG);
            config.hwndParent = hwnd;
            config.pszWindowTitle = wTitle.c_str();
            config.pszMainInstruction = wMessage.c_str();
            config.dwCommonButtons = TDCBF_OK_BUTTON;
            config.pszMainIcon = TD_INFORMATION_ICON;

            int pressed = 0;

            if (runTaskDialog(config, pressed)) {
                return fromTaskButton(pressed);
            }

            return fromTaskButton(MessageBoxW(
                hwnd,
                wMessage.c_str(),
                wTitle.c_str(),
                MB_OK | MB_ICONINFORMATION
            ));
        }

        static DialogResult Warning(
            const std::string& title,
            const std::string& message,
            void* owner = nullptr
        ) {
            HWND hwnd = static_cast<HWND>(owner);
            std::wstring wTitle = widen(title);
            std::wstring wMessage = widen(message);

            TASKDIALOGCONFIG config = {};
            config.cbSize = sizeof(TASKDIALOGCONFIG);
            config.hwndParent = hwnd;
            config.pszWindowTitle = wTitle.c_str();
            config.pszMainInstruction = wMessage.c_str();
            config.dwCommonButtons = TDCBF_OK_BUTTON;
            config.pszMainIcon = TD_WARNING_ICON;

            int pressed = 0;

            if (runTaskDialog(config, pressed)) {
                return fromTaskButton(pressed);
            }

            return fromTaskButton(MessageBoxW(
                hwnd,
                wMessage.c_str(),
                wTitle.c_str(),
                MB_OK | MB_ICONWARNING
            ));
        }

        static DialogResult Error(
            const std::string& title,
            const std::string& message,
            void* owner = nullptr
        ) {
            HWND hwnd = static_cast<HWND>(owner);
            std::wstring wTitle = widen(title);
            std::wstring wMessage = widen(message);

            TASKDIALOGCONFIG config = {};
            config.cbSize = sizeof(TASKDIALOGCONFIG);
            config.hwndParent = hwnd;
            config.pszWindowTitle = wTitle.c_str();
            config.pszMainInstruction = wMessage.c_str();
            config.dwCommonButtons = TDCBF_OK_BUTTON;
            config.pszMainIcon = TD_ERROR_ICON;

            int pressed = 0;

            if (runTaskDialog(config, pressed)) {
                return fromTaskButton(pressed);
            }

            return fromTaskButton(MessageBoxW(
                hwnd,
                wMessage.c_str(),
                wTitle.c_str(),
                MB_OK | MB_ICONERROR
            ));
        }

        static DialogResult Confirm(
            const std::string& title,
            const std::string& message,
            void* owner = nullptr
        ) {
            std::wstring wTitle = widen(title);
            std::wstring wMessage = widen(message);

            DialogResult result = showTask(
                owner,
                wTitle,
                wMessage,
                L"",
                TDCBF_YES_BUTTON | TDCBF_NO_BUTTON,
                IDYES
            );

            if (result != DialogResult::None) {
                return result;
            }

            return fromTaskButton(MessageBoxW(
                static_cast<HWND>(owner),
                wMessage.c_str(),
                wTitle.c_str(),
                MB_YESNO | MB_ICONQUESTION
            ));
        }

        static UnsavedChangesResult UnsavedChanges(
            const std::string& itemName = "Untitled Project",
            void* owner = nullptr
        ) {
            HWND hwnd = static_cast<HWND>(owner);
            std::wstring title = L"Unsaved Changes";
            std::wstring instruction =
                L"Do you want to save changes to \"" +
                widen(itemName) +
                L"\"?";

            TASKDIALOG_BUTTON buttons[] = {
                { 100, L"Save" },
                { 101, L"Don't save" },
                { 102, L"Cancel" }
            };

            TASKDIALOGCONFIG config = {};
            config.cbSize = sizeof(TASKDIALOGCONFIG);
            config.hwndParent = hwnd;
            config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
            config.pszWindowTitle = title.c_str();
            config.pszMainInstruction = instruction.c_str();
            config.cButtons = 3;
            config.pButtons = buttons;
            config.nDefaultButton = 100;
            config.pszMainIcon = TD_WARNING_ICON;

            int pressed = 0;

            if (runTaskDialog(config, pressed)) {

                if (pressed == 100) {
                    return UnsavedChangesResult::Save;
                }

                if (pressed == 101) {
                    return UnsavedChangesResult::Discard;
                }

                return UnsavedChangesResult::Cancel;
            }

            int result = MessageBoxW(
                hwnd,
                instruction.c_str(),
                title.c_str(),
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

        static DialogResult fromMessageBoxResult(int result) {

            switch (result) {

                case IDOK: { return DialogResult::Ok; }
                case IDCANCEL: { return DialogResult::Cancel; }
                case IDYES: { return DialogResult::Yes; }
                case IDNO: { return DialogResult::No; }

                default: { return DialogResult::None; }
            }
        }
    };
}
