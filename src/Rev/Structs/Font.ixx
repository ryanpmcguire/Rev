module;

#include <vector>
#include <stdexcept>
#include <cstring>

#include <ft2build.h>
#include FT_FREETYPE_H

export module Rev.Font;

import Resource;
import Resources.Fonts.Roboto.Roboto_ttf;

export namespace Rev {

    struct Font {

        Resource resource = Roboto_ttf;
        float size = 50;

        // FreeType handles
        FT_Library ft = nullptr;
        FT_Face face = nullptr;

        struct Glyph {
            float advance;
            float bearingX;
            float bearingY;
            float width;
            float height;
            float u0, v0, u1, v1; // Texture coords
        };

        std::vector<Glyph> glyphs;

        float ascentPx, descentPx, lineGapPx, lineHeightPx;

        struct Bitmap {
            int width = 0, height = 0;
            size_t size = 0;
            unsigned char* data = nullptr;
        } bitmap;

        Font(Resource resource = Roboto_ttf) : resource(resource) {
            init();
            bake();
        }

        ~Font() {
            if (face) FT_Done_Face(face);
            if (ft) FT_Done_FreeType(ft);
            if (bitmap.data) delete[] bitmap.data;
        }

        void init() {
            if (FT_Init_FreeType(&ft))
                throw std::runtime_error("Failed to initialize FreeType");

            if (FT_New_Memory_Face(ft, resource.data, static_cast<FT_Long>(resource.size), 0, &face))
                throw std::runtime_error("Failed to load font from memory");

            if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(size)))
                throw std::runtime_error("Failed to set font pixel size");

            ascentPx = face->size->metrics.ascender / 64.0f;
            descentPx = fabs(face->size->metrics.descender / 64.0f);
            lineGapPx = (face->size->metrics.height - (face->size->metrics.ascender - face->size->metrics.descender)) / 64.0f;
            lineHeightPx = face->size->metrics.height / 64.0f;

            glyphs.resize(256);
        }

        void bake() {
            const int padding = 1;
            int penX = padding;
            int penY = padding;
            int rowHeight = 0;

            bitmap.width = 4096;
            bitmap.height = 4096;
            bitmap.size = bitmap.width * bitmap.height;
            bitmap.data = new unsigned char[bitmap.size];
            std::memset(bitmap.data, 0, bitmap.size);

            for (char c = 32; c < 127; c++) {
                if (FT_Load_Char(face, c, FT_LOAD_RENDER))
                    continue;

                FT_GlyphSlot g = face->glyph;
                if (penX + g->bitmap.width + padding >= bitmap.width) {
                    penX = padding;
                    penY += rowHeight + padding;
                    rowHeight = 0;
                }

                if (penY + g->bitmap.rows >= bitmap.height)
                    throw std::runtime_error("Font atlas overflowed");

                // Copy glyph bitmap into atlas
                for (int y = 0; y < g->bitmap.rows; y++) {
                    for (int x = 0; x < g->bitmap.width; x++) {
                        int dst = (penX + x) + (penY + y) * bitmap.width;
                        int src = x + y * g->bitmap.pitch;
                        bitmap.data[dst] = g->bitmap.buffer[src];
                    }
                }

                Glyph& glyph = glyphs[c];
                glyph.advance = static_cast<float>(g->advance.x) / 64.0f;
                glyph.bearingX = static_cast<float>(g->bitmap_left);
                glyph.bearingY = static_cast<float>(g->bitmap_top);
                glyph.width = static_cast<float>(g->bitmap.width);
                glyph.height = static_cast<float>(g->bitmap.rows);

                glyph.u0 = static_cast<float>(penX) / bitmap.width;
                glyph.v0 = static_cast<float>(penY) / bitmap.height;
                glyph.u1 = static_cast<float>(penX + g->bitmap.width) / bitmap.width;
                glyph.v1 = static_cast<float>(penY + g->bitmap.rows) / bitmap.height;

                penX += g->bitmap.width + padding;
                rowHeight = std::max(rowHeight, static_cast<int>(g->bitmap.rows));
            }

            bool test = true;
        }

        // Lookup glyph and calculate quad position and texture coords
        Glyph getGlyph(char c) const {
            return glyphs[static_cast<unsigned char>(c)];
        }

        struct Quad {
            float x0, y0, s0, t0;
            float x1, y1, s1, t1;
        };
        
        Quad getQuad(char c, float& x, float& y) const {

            const Glyph& g = glyphs[static_cast<unsigned char>(c)];
        
            float x0 = x + g.bearingX;
            float y0 = y - g.bearingY;
            float x1 = x0 + g.width;
            float y1 = y0 + g.height;
        
            Quad q = {
                .x0 = x0, .y0 = y0, .s0 = g.u0, .t0 = g.v0,
                .x1 = x1, .y1 = y1, .s1 = g.u1, .t1 = g.v1
            };
        
            x += g.advance;
            return q;
        }
    };
};
