module;

#include <string>
#include <vector>
#include <cstdint>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb/stb_image_resize2.h>

export module LithoControl.ImageDecode;

export namespace LithoControl {

    // Decode a PNG/JPEG/BMP file into a tightly-packed RGBA buffer.
    inline bool decodeToRGBA(const std::string& path, std::vector<uint8_t>& out, int& w, int& h) {
        int channels = 0;
        stbi_uc* px = stbi_load(path.c_str(), &w, &h, &channels, 4);
        if (!px || w <= 0 || h <= 0) {
            if (px) stbi_image_free(px);
            return false;
        }

        out.assign(px, px + (size_t)w * h * 4);
        stbi_image_free(px);
        return true;
    }

    // Decode and resize (bilinear) to exactly dstW x dstH RGBA.
    inline bool decodeToRGBAResized(const std::string& path, std::vector<uint8_t>& out,
                                     int dstW, int dstH) {
        std::vector<uint8_t> src;
        int w = 0, h = 0;
        if (!decodeToRGBA(path, src, w, h)) return false;

        if (w == dstW && h == dstH) {
            out = std::move(src);
            return true;
        }

        out.assign((size_t)dstW * dstH * 4, 0);
        return stbir_resize_uint8_linear(
            src.data(), w, h, 0,
            out.data(), dstW, dstH, 0,
            STBIR_RGBA
        ) != nullptr;
    }
}
