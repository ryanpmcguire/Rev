module;

#include <cmath>
#include <vector>
#include <map>
#include <stdexcept>
#include <cstring>

#include <ft2build.h>
#include FT_FREETYPE_H

export module Rev.Font;

import Rev.Graphics.Canvas;
import Rev.Graphics.UniformBuffer;
import Rev.Graphics.Texture;

import Resource;
import Resources.Fonts.Arial.Arial_ttf;

export namespace Rev {

    struct Font {

        // Font details
        //--------------------------------------------------

        // The font resource
        Resource resource = Arial_ttf;

        // FreeType handles
        inline static FT_Library ft = nullptr;
        inline static size_t users = 0;
        FT_Face face = nullptr;

        // Font attributes
        float scale = 1.0f;
        const int padding = 4;
        const float rPadding = 0;

        float size = 12.0f;
        int weight = 200;
        float ascent, descent;
        float lineGap, lineHeight;

        // Glyphs
        //--------------------------------------------------

        struct Glyph {

            size_t index;

            float width, height;

            float bearingX, bearingY;
            float advance;
            
            float kerning[256] = { 0.0f };

            float u0, v0, u1, v1; // Texture coords
        };

        std::vector<Glyph> glyphs;

        // Bitmap texture
        //--------------------------------------------------

        struct Bitmap {
            
            int width, height;
            size_t size;

            unsigned char* data = nullptr;
        };

        Bitmap bitmap;
        Texture* texture = nullptr;

        // Glyph data
        //--------------------------------------------------

        // Position and texture coords
        struct CharVertex {
            float x, y;
            float u, v;
        };

        // Six vertices per quad
        struct GlyphData {
            CharVertex a, b, c, d, e, f;
        };

        UniformBuffer* glyphData = nullptr;

        Font(Canvas* canvas, Resource resource = Arial_ttf, float size = 12.0f, float scale = 1.0f) : resource(resource) {

            this->size = size;
            this->scale = scale;
            
            // Initialize freetype if we are the first user
            if (++users == 1 && FT_Init_FreeType(&ft)) {
                throw std::runtime_error("Failed to initialize FreeType");
            }

            // Load font face from memory (resource data)
            if (FT_New_Memory_Face(ft, resource.data, static_cast<FT_Long>(resource.size), 0, &face)) {
                throw std::runtime_error("Failed to load font from memory");
            }

            // Set pixel size
            if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(scale * size))) {
                throw std::runtime_error("Failed to set font pixel size");
            }

            glyphs.resize(256);

            this->getFontAttribs();
            this->bake();

            texture = new Texture(canvas->context, bitmap.data, bitmap.width, bitmap.height, 1);

            // Make glyph data ubo
            //--------------------------------------------------

            glyphData = new UniformBuffer(canvas->context, sizeof(GlyphData) * 128);
            GlyphData* glyphDataArr = static_cast<GlyphData*>(glyphData->data);

            for (char c = 0; c < 127; c++) {

                Font::Quad q = this->getRelativeQuad(c);

                glyphDataArr[c] = {

                    // Triangle 1
                    { q.x0, q.y0, q.s0, q.t0 },
                    { q.x1, q.y0, q.s1, q.t0 },
                    { q.x1, q.y1, q.s1, q.t1 },
    
                    // Triangle 2
                    { q.x0, q.y0, q.s0, q.t0 },
                    { q.x1, q.y1, q.s1, q.t1 },
                    { q.x0, q.y1, q.s0, q.t1 }
                };
            }
        }

        ~Font() {

            // Delete our resources
            if (face) { FT_Done_Face(face); }
            if (bitmap.data) { delete[] bitmap.data; }
            if (texture) { delete texture; }
            if (glyphData) { delete glyphData; }

            // Delete freetype if we are the last user
            if (--users == 0 && ft) {
                FT_Done_FreeType(ft);
            }
        }

        // Get font face atributes
        void getFontAttribs() {

            // Ascent, descent, line, etc...
            ascent = (1.0f / scale) * face->size->metrics.ascender / 64.0f;
            descent = (1.0f / scale) * fabs(face->size->metrics.descender / 64.0f);
            lineGap = (1.0f / scale) * (face->size->metrics.height - (face->size->metrics.ascender - face->size->metrics.descender)) / 64.0f;
            lineHeight = (1.0f / scale) * face->size->metrics.height / 64.0f;

            // Glyphs
            //--------------------------------------------------

            for (size_t i = 0; i < 127; i++) {
                glyphs[i].index = FT_Get_Char_Index(face, static_cast<FT_ULong>(i));
            }

            // Kerning
            //--------------------------------------------------

            if (!FT_HAS_KERNING(face) ) { return; }

            // Compute for left/right combos
            for (size_t l = 0; l < 127; l++) {
                for (size_t r = 0; r < 127; r++) {

                    Glyph& left = glyphs[l];
                    Glyph& right = glyphs[r];

                    FT_Vector delta = { 0, 0 };
                    FT_Get_Kerning(face, left.index, right.index, FT_KERNING_DEFAULT, &delta);
                    
                    right.kerning[left.index] = (1.0f / scale) * static_cast<float>(delta.x) / 64.0f;
                }
            }
        }

        // A crucial part of font rendering is pre-baking the font into a bitmap texture
        // which can be fed to the GPU, and for each glyph the texture coordinates thereof
        // being used to write said glyph at the correct position on the screen.
        void bake() {
        
            int penX = padding;
            int penY = padding;
            int rowHeight = 0;

            bitmap.width = 1024;
            bitmap.height = 1024;
            bitmap.size = bitmap.width * bitmap.height;
            bitmap.data = new unsigned char[bitmap.size];
            std::memset(bitmap.data, 0, bitmap.size);

            for (char c = 32; c < 127; c++) {

                if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
                    continue;
                }

                FT_GlyphSlot g = face->glyph;

                if (penX + g->bitmap.width + padding >= bitmap.width) {
                    penX = padding;
                    penY += rowHeight + padding;
                    rowHeight = 0;
                }

                if (penY + g->bitmap.rows >= bitmap.height) {
                    throw std::runtime_error("Font atlas overflowed");
                }

                // Copy glyph bitmap into atlas
                for (int y = 0; y < g->bitmap.rows; y++) {
                    for (int x = 0; x < g->bitmap.width; x++) {
                        int dst = (penX + x) + (penY + y) * bitmap.width;
                        int src = x + y * g->bitmap.pitch;
                        bitmap.data[dst] = g->bitmap.buffer[src];
                    }
                }

                Glyph& glyph = glyphs[c];

                glyphs[c] = {

                    .width = (1.0f / scale) * float(g->bitmap.width),
                    .height = (1.0f / scale) * float(g->bitmap.rows),
                    .bearingX = (1.0f / scale) * float(g->bitmap_left),
                    .bearingY = (1.0f / scale) * float(g->bitmap_top),
                    .advance = (1.0f / scale) * float(g->advance.x) / 64.0f,

                    .u0 = (float(penX) - scale * rPadding) / bitmap.width,
                    .v0 = (float(penY) - scale * rPadding) / bitmap.height,
                    .u1 = (float(penX + g->bitmap.width) + scale * rPadding) / bitmap.width,
                    .v1 = (float(penY + g->bitmap.rows) + scale * rPadding)/ bitmap.height
                };

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
        
        Quad getQuad(char c, float& x, float& y, char prev = 0) {

            const Glyph& g = glyphs[static_cast<unsigned char>(c)];
        
            float kerning = glyphs[prev].kerning[c];

            x += kerning;

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

        Quad getRelativeQuad(char c, char prev = 0) {
            
            const Glyph& g = glyphs[static_cast<unsigned char>(c)];
        
            // Optional: kerning between prev and c, not strictly needed for static glyph data
            float kerning = glyphs[static_cast<unsigned char>(prev)].kerning[c];
        
            float penX = 0.0f;
            float penY = 0.0f;
        
            float x0 = penX + g.bearingX - scale * rPadding;
            float y0 = penY - g.bearingY - scale * rPadding;
            float x1 = x0 + g.width + 2.0f * scale * rPadding;
            float y1 = y0 + g.height + 2.0f * scale * rPadding;
        
            return {
                .x0 = x0, .y0 = y0, .s0 = g.u0, .t0 = g.v0,
                .x1 = x1, .y1 = y1, .s1 = g.u1, .t1 = g.v1
            };
        }
    };
};