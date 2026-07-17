module;

#include <windows.h>
#include <string>
#include <cstring>

export module Rev.OS.Clipboard;

export namespace Rev::OS {

    struct Clipboard {

        static bool SetText(const std::string& text) {
            if (!OpenClipboard(nullptr)) return false;
            EmptyClipboard();

            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
            bool ok = false;
            if (hMem) {
                auto* dst = static_cast<char*>(GlobalLock(hMem));
                std::memcpy(dst, text.c_str(), text.size() + 1);
                GlobalUnlock(hMem);
                ok = SetClipboardData(CF_TEXT, hMem) != nullptr;
            }

            CloseClipboard();
            return ok;
        }
    };
}
