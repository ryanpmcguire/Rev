module;

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

export module Rev.OS.Clipboard;

export namespace Rev::OS {

    // An image read from the clipboard: straight (non-premultiplied) RGBA8, top-down rows.
    struct ClipboardImage {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgba;
    };

    // TODO: read NSPasteboard's "public.png" (then "public.tiff") and decode with stb.
    inline std::optional<ClipboardImage> ReadImage(void* owner = nullptr) {
        (void)owner;
        return std::nullopt;
    }

    // TODO: read NSPasteboard's "public.file-url" items.
    inline std::vector<std::wstring> ReadFiles(void* owner = nullptr) {
        (void)owner;
        return {};
    }
}
