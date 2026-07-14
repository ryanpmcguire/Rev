module;

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>

export module LithoControl.TestPatternRaster;

// Minimal software rasterizer for the HDMI focus/calibration test pattern.
// Operates directly on a tightly-packed BGRA8 buffer (matches the layout the
// old GDI+ PixelFormat32bppARGB path produced). Not a general 2D API --
// only the handful of primitives runTestAnimation() needs.
export namespace LithoControl::Raster {

    struct Color { uint8_t b, g, r, a; };

    inline void blend(uint8_t* buf, int W, int H, int x, int y, Color c) {
        if (x < 0 || y < 0 || x >= W || y >= H) return;
        uint8_t* p = buf + (y * W + x) * 4;
        if (c.a >= 255) { p[0] = c.b; p[1] = c.g; p[2] = c.r; p[3] = 255; return; }
        int a = c.a, ia = 255 - a;
        p[0] = (uint8_t)((c.b * a + p[0] * ia) / 255);
        p[1] = (uint8_t)((c.g * a + p[1] * ia) / 255);
        p[2] = (uint8_t)((c.r * a + p[2] * ia) / 255);
        p[3] = 255;
    }

    inline void clear(uint8_t* buf, int W, int H, Color c) {
        for (int i = 0; i < W * H; i++) {
            buf[i*4+0] = c.b; buf[i*4+1] = c.g; buf[i*4+2] = c.r; buf[i*4+3] = 255;
        }
    }

    inline void fillRect(uint8_t* buf, int W, int H, int x0, int y0, int w, int h, Color c) {
        int x1 = x0 + w, y1 = y0 + h;
        x0 = (std::max)(x0, 0); y0 = (std::max)(y0, 0);
        x1 = (std::min)(x1, W); y1 = (std::min)(y1, H);
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                blend(buf, W, H, x, y, c);
    }

    inline void drawLine(uint8_t* buf, int W, int H, float x0, float y0, float x1, float y1,
                          Color c, float thickness = 1.0f) {
        float dx = x1 - x0, dy = y1 - y0;
        float len = std::sqrt(dx*dx + dy*dy);
        int steps = (int)(std::max)(len, 1.0f);
        float half = thickness * 0.5f;
        int ihalf = (std::max)(1, (int)std::ceil(half));

        for (int i = 0; i <= steps; i++) {
            float t = (float)i / (float)steps;
            int cx = (int)(x0 + dx * t);
            int cy = (int)(y0 + dy * t);
            for (int oy = -ihalf; oy <= ihalf; oy++)
                for (int ox = -ihalf; ox <= ihalf; ox++)
                    if (ox*ox + oy*oy <= ihalf*ihalf)
                        blend(buf, W, H, cx + ox, cy + oy, c);
        }
    }

    inline void fillCircle(uint8_t* buf, int W, int H, float cx, float cy, float radius, Color c) {
        int x0 = (int)std::floor(cx - radius), x1 = (int)std::ceil(cx + radius);
        int y0 = (int)std::floor(cy - radius), y1 = (int)std::ceil(cy + radius);
        float r2 = radius * radius;
        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                float dx = x - cx, dy = y - cy;
                if (dx*dx + dy*dy <= r2) blend(buf, W, H, x, y, c);
            }
        }
    }

    inline void strokeCircle(uint8_t* buf, int W, int H, float cx, float cy, float radius,
                              Color c, float thickness = 1.0f) {
        int segments = (std::max)(24, (int)(radius * 0.5f));
        float prevX = cx + radius, prevY = cy;
        for (int i = 1; i <= segments; i++) {
            float a = (float)i / segments * 6.28318530718f;
            float x = cx + std::cos(a) * radius;
            float y = cy + std::sin(a) * radius;
            drawLine(buf, W, H, prevX, prevY, x, y, c, thickness);
            prevX = x; prevY = y;
        }
    }

    // Bitmap font baked once from a TTF file on disk (kept tiny -- no packer,
    // no atlas resource pipeline; this is a standalone diagnostic overlay).
    struct BitmapFont {
        static constexpr int ATLAS_SIZE = 512;
        std::vector<unsigned char> atlas;
        std::vector<stbtt_bakedchar> chars;
        int firstChar = 32, numChars = 96;
        bool loaded = false;

        bool load(const std::string& ttfPath, float pixelHeight) {
            std::ifstream f(ttfPath, std::ios::binary);
            if (!f) return false;
            std::vector<unsigned char> data((std::istreambuf_iterator<char>(f)),
                                             std::istreambuf_iterator<char>());
            if (data.empty()) return false;

            atlas.assign(ATLAS_SIZE * ATLAS_SIZE, 0);
            chars.assign(numChars, {});

            int result = stbtt_BakeFontBitmap(
                data.data(), 0, pixelHeight,
                atlas.data(), ATLAS_SIZE, ATLAS_SIZE,
                firstChar, numChars, chars.data());

            loaded = result > 0;
            return loaded;
        }

        // Returns the advanced x position after drawing.
        float draw(uint8_t* buf, int W, int H, const std::string& text,
                   float x, float y, Color c,
                   int clipX0 = 0, int clipY0 = 0, int clipX1 = -1, int clipY1 = -1) const {
            if (!loaded) return x;
            if (clipX1 < 0) clipX1 = W;
            if (clipY1 < 0) clipY1 = H;

            for (unsigned char ch : text) {
                if (ch < firstChar || ch >= firstChar + numChars) { x += 6.0f; continue; }

                stbtt_aligned_quad q;
                stbtt_GetBakedQuad(chars.data(), ATLAS_SIZE, ATLAS_SIZE,
                                   ch - firstChar, &x, &y, &q, 1);

                int gx0 = (int)q.x0, gy0 = (int)q.y0, gx1 = (int)q.x1, gy1 = (int)q.y1;
                int gw = gx1 - gx0, gh = gy1 - gy0;
                if (gw <= 0 || gh <= 0) continue;

                float us = q.s1 - q.s0, vs = q.t1 - q.t0;

                for (int py = 0; py < gh; py++) {
                    int dy = gy0 + py;
                    if (dy < clipY0 || dy >= clipY1) continue;
                    for (int px = 0; px < gw; px++) {
                        int dx = gx0 + px;
                        if (dx < clipX0 || dx >= clipX1) continue;

                        float u = q.s0 + us * ((float)px / gw);
                        float v = q.t0 + vs * ((float)py / gh);
                        int au = (int)(u * ATLAS_SIZE);
                        int av = (int)(v * ATLAS_SIZE);
                        if (au < 0 || av < 0 || au >= ATLAS_SIZE || av >= ATLAS_SIZE) continue;

                        uint8_t coverage = atlas[av * ATLAS_SIZE + au];
                        if (coverage == 0) continue;

                        Color blended = c;
                        blended.a = (uint8_t)((int)c.a * coverage / 255);
                        blend(buf, W, H, dx, dy, blended);
                    }
                }
            }
            return x;
        }

        float measure(const std::string& text) const {
            if (!loaded) return 0.0f;
            float x = 0.0f, y = 0.0f;
            for (unsigned char ch : text) {
                if (ch < firstChar || ch >= firstChar + numChars) { x += 6.0f; continue; }
                stbtt_aligned_quad q;
                stbtt_GetBakedQuad(chars.data(), ATLAS_SIZE, ATLAS_SIZE,
                                   ch - firstChar, &x, &y, &q, 1);
            }
            return x;
        }
    };
}
