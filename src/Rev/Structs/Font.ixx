module;

#include <vector>
#include <stdexcept>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>

export module Rev.Font;

import Resource;
import Resources.Fonts.Roboto.Roboto_ttf;

export namespace Rev {

    struct Font {

        Resource resource = Roboto_ttf;

        float size = 100;
        float scale;

        stbtt_bakedchar bakedChars[96];
        stbtt_fontinfo fontInfo;
        
        // Unit-based info
        int ascent, descent, lineGap;
        std::vector<int> advance, leftBearing;

        // Px-based info
        float ascentPx, descentPx, lineGapPx;
        std::vector<float> advancePx, leftBearingPx;

        struct Bitmap {
            int width = 128, height = 128;
            size_t size = sizeof(unsigned char) * 128 * 128;
            unsigned char* data = nullptr;
        };

        Bitmap bitmap;

        Font(Resource resource = Roboto_ttf) {

            this->resource = resource;

            this->init();
            this->bake();
        }

        ~Font() {

        }

        void init() {

            // Init font with scale
            stbtt_InitFont(&fontInfo, resource.data, 0);
            scale = stbtt_ScaleForPixelHeight(&fontInfo, size);

            // Get vertical metrics
            //--------------------------------------------------

            stbtt_GetFontVMetrics(&fontInfo, &ascent, &descent, &lineGap);

            ascentPx = scale * ascent;
            descentPx = fabs(scale * descent);
            lineGapPx = scale * lineGap;

            // Get horizontal metrics
            //--------------------------------------------------

            // Make space for advance and left bearing
            advance.resize(128); advancePx.resize(128);
            leftBearing.resize(128); leftBearingPx.resize(128);

            // Measure advance and left bearing for all characters
            for (int c = 0; c < 128; c++) {

                stbtt_GetCodepointHMetrics(&fontInfo, c, &advance[c], &leftBearing[c]);

                advancePx[c] = scale * advance[c];
                leftBearingPx[c] = scale * leftBearing[c];
            }
        }

        void bake() {

            int dim = 2048;

            while (dim < 4096) {

                // Resize
                bitmap.size = sizeof(unsigned char) * dim * dim;
                bitmap.width = dim; bitmap.height = dim;

                // Realloc
                if (bitmap.data) { delete bitmap.data; }
                bitmap.data = new unsigned char[bitmap.size];
            
                // Attempt bake
                int result = stbtt_BakeFontBitmap(
                    resource.data, 0,
                    size, bitmap.data,
                    bitmap.width, bitmap.height,
                    32, 96, bakedChars
                );

                // Break or double size
                if (result > 0) { break; }
                else { dim *= 2; }
            }

            //std::memset(bitmap.data, 100, bitmap.width * bitmap.height);

            // If final dim was too large
            if (dim > 4096) {
                throw std::runtime_error("Font size (or glyphs) was/were too large to bake!");
            }
        }

        stbtt_aligned_quad getQuad(char c, float& x, float& y) const {
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(bakedChars, bitmap.width, bitmap.height, c - 32, &x, &y, &q, 1);
            return q;
        }
    };
};