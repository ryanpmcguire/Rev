module;

#include <string>
#include <cstdint>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

export module LithoControl.ImageEncode;

export namespace LithoControl {

    // Encode a tightly-packed RGBA buffer to a PNG file.
    inline bool encodeRGBAToPng(const std::string& path, const uint8_t* rgba, int w, int h) {
        if (!rgba || w <= 0 || h <= 0) return false;
        return stbi_write_png(path.c_str(), w, h, 4, rgba, w * 4) != 0;
    }
}
