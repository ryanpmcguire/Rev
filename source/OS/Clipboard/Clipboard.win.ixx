module;

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <windows.h>
#include <shellapi.h>   // DragQueryFileW (CF_HDROP)

#include <stb/StbImage.h>   // implementation compiled once, in Rev.Element.Image

export module Rev.OS.Clipboard;

export namespace Rev::OS {

    // An image read from the clipboard: straight (non-premultiplied) RGBA8, top-down rows.
    struct ClipboardImage {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgba;
    };

    struct Clipboard {

        // Opens the clipboard for the duration of one read; closes it on every path.
        struct Session {
            bool open;
            explicit Session(void* owner) : open(OpenClipboard(static_cast<HWND>(owner)) != 0) {}
            ~Session() { if (open) CloseClipboard(); }
            Session(const Session&) = delete;
            Session& operator=(const Session&) = delete;
        };

        // Locks a clipboard HGLOBAL for the duration of one decode.
        struct Lock {
            HGLOBAL handle;
            const uint8_t* data;
            size_t size;
            explicit Lock(HANDLE h)
                : handle(static_cast<HGLOBAL>(h)),
                  data(handle ? static_cast<const uint8_t*>(GlobalLock(handle)) : nullptr),
                  size(data ? GlobalSize(handle) : 0) {}
            ~Lock() { if (data) GlobalUnlock(handle); }
            Lock(const Lock&) = delete;
            Lock& operator=(const Lock&) = delete;
        };

        static std::optional<ClipboardImage> decodePng(const uint8_t* data, size_t size) {
            int w = 0, h = 0, channels = 0;
            unsigned char* pixels = stbi_load_from_memory(data, (int)size, &w, &h, &channels, 4);
            if (!pixels) return std::nullopt;
            ClipboardImage image{ w, h, std::vector<uint8_t>(pixels, pixels + (size_t)w * h * 4) };
            stbi_image_free(pixels);
            return image;
        }

        static int shiftOf(uint32_t mask) {
            if (!mask) return 0;
            int s = 0;
            while (!(mask & 1u)) { mask >>= 1; ++s; }
            return s;
        }

        static uint8_t channel(uint32_t pixel, uint32_t mask) {
            if (!mask) return 0;
            uint32_t v = (pixel & mask) >> shiftOf(mask);
            uint32_t max = mask >> shiftOf(mask);
            return (uint8_t)((v * 255u + max / 2) / max);
        }

        // Decodes a packed DIB (CF_DIB or CF_DIBV5): 24- or 32-bit, BI_RGB or BI_BITFIELDS.
        // honourAlpha: use the alpha mask (CF_DIBV5); otherwise the image is opaque.
        static std::optional<ClipboardImage> decodeDib(const uint8_t* data, size_t size, bool honourAlpha) {
            if (size < sizeof(BITMAPINFOHEADER)) return std::nullopt;
            BITMAPINFOHEADER header;
            std::memcpy(&header, data, sizeof(header));
            if (header.biSize < sizeof(BITMAPINFOHEADER) || header.biSize > size) return std::nullopt;

            const int bits = header.biBitCount;
            if (bits != 24 && bits != 32) return std::nullopt;
            if (header.biCompression != BI_RGB && header.biCompression != BI_BITFIELDS) return std::nullopt;

            uint32_t rMask = 0x00FF0000, gMask = 0x0000FF00, bMask = 0x000000FF, aMask = 0;
            size_t offset = header.biSize;
            if (header.biCompression == BI_BITFIELDS) {
                if (header.biSize >= sizeof(BITMAPV4HEADER)) {
                    BITMAPV5HEADER v5{};
                    std::memcpy(&v5, data, header.biSize < sizeof(v5) ? header.biSize : sizeof(v5));
                    rMask = v5.bV5RedMask; gMask = v5.bV5GreenMask; bMask = v5.bV5BlueMask; aMask = v5.bV5AlphaMask;
                } else {
                    if (offset + 12 > size) return std::nullopt;
                    std::memcpy(&rMask, data + offset, 4);
                    std::memcpy(&gMask, data + offset + 4, 4);
                    std::memcpy(&bMask, data + offset + 8, 4);
                    offset += 12;
                }
            } else if (bits == 32 && header.biSize >= sizeof(BITMAPV4HEADER)) {
                BITMAPV5HEADER v5{};
                std::memcpy(&v5, data, header.biSize < sizeof(v5) ? header.biSize : sizeof(v5));
                aMask = v5.bV5AlphaMask;
            }
            if (!honourAlpha) aMask = 0;
            offset += (size_t)header.biClrUsed * 4;

            const int width = header.biWidth;
            const bool bottomUp = header.biHeight > 0;
            const int height = bottomUp ? header.biHeight : -header.biHeight;
            if (width <= 0 || height <= 0) return std::nullopt;

            const size_t stride = (((size_t)width * bits + 31) / 32) * 4;
            if (offset + stride * height > size) return std::nullopt;

            ClipboardImage image{ width, height, std::vector<uint8_t>((size_t)width * height * 4) };
            bool anyAlpha = false;
            for (int y = 0; y < height; ++y) {
                const uint8_t* row = data + offset + stride * (bottomUp ? height - 1 - y : y);
                uint8_t* out = image.rgba.data() + (size_t)y * width * 4;
                for (int x = 0; x < width; ++x) {
                    uint32_t p = 0;
                    std::memcpy(&p, row + (size_t)x * (bits / 8), bits / 8);
                    out[x * 4 + 0] = channel(p, rMask);
                    out[x * 4 + 1] = channel(p, gMask);
                    out[x * 4 + 2] = channel(p, bMask);
                    out[x * 4 + 3] = aMask ? channel(p, aMask) : 255;
                    anyAlpha |= out[x * 4 + 3] != 0;
                }
            }
            // An alpha mask with every pixel zero means the writer left alpha unused: opaque.
            if (aMask && !anyAlpha)
                for (size_t i = 3; i < image.rgba.size(); i += 4) image.rgba[i] = 255;
            return image;
        }
    };

    // Reads an image from the system clipboard. Order: registered "PNG", CF_DIBV5, CF_DIB.
    // owner: the native window handle (HWND) or nullptr. The clipboard is never held across calls.
    inline std::optional<ClipboardImage> ReadImage(void* owner = nullptr) {
        Clipboard::Session session(owner);
        if (!session.open) return std::nullopt;

        static const UINT png = RegisterClipboardFormatW(L"PNG");
        if (png && IsClipboardFormatAvailable(png)) {
            Clipboard::Lock lock(GetClipboardData(png));
            if (lock.data)
                if (auto image = Clipboard::decodePng(lock.data, lock.size)) return image;
        }
        if (IsClipboardFormatAvailable(CF_DIBV5)) {
            Clipboard::Lock lock(GetClipboardData(CF_DIBV5));
            if (lock.data)
                if (auto image = Clipboard::decodeDib(lock.data, lock.size, true)) return image;
        }
        if (IsClipboardFormatAvailable(CF_DIB)) {
            Clipboard::Lock lock(GetClipboardData(CF_DIB));
            if (lock.data)
                if (auto image = Clipboard::decodeDib(lock.data, lock.size, false)) return image;
        }
        return std::nullopt;
    }

    // Reads the file paths on the clipboard (CF_HDROP, e.g. files copied in Explorer).
    // Paths only, never decoded: the same shape as Event::DroppedFiles::paths.
    inline std::vector<std::wstring> ReadFiles(void* owner = nullptr) {
        std::vector<std::wstring> paths;
        Clipboard::Session session(owner);
        if (!session.open || !IsClipboardFormatAvailable(CF_HDROP)) return paths;
        // The handle itself is the HDROP; DragQueryFileW locks it internally.
        HDROP drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
        if (!drop) return paths;
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            const UINT length = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring path(length, L'\0');
            DragQueryFileW(drop, i, path.data(), length + 1);
            paths.push_back(std::move(path));
        }
        return paths;
    }
}
