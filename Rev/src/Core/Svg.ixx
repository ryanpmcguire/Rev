module;

#include <cmath>
#include <cstring>
#include <stdexcept>

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvg/nanosvg.h>
#include <nanosvg/nanosvgrast.h>

#include <dbg.hpp>

export module Rev.Core.Svg;

import Rev.Core.Resource;

import Rev.Graphics.Canvas;
import Rev.Graphics.Texture;

export namespace Rev::Core {

    using namespace Rev::Graphics;

    struct Svg {

        // Bitmap texture
        //--------------------------------------------------

        NSVGimage* image = nullptr;
        NSVGrasterizer* rast = nullptr;

        unsigned char* pixels = nullptr;
        float width = 0;
        float height = 0;
        float scale = 1.0f;

        Rev::Graphics::Canvas* canvas = nullptr;
        Rev::Graphics::Texture* texture = nullptr;

        struct Bitmap {
            
            size_t width, height;
            size_t size;

            unsigned char* data = nullptr;
        };

        Resource resource;
        Bitmap bitmap;

        Svg(Canvas* canvas, Resource resource) {

            this->canvas = canvas;
            this->resource = resource;

            bitmap.width = 1;
            bitmap.height = 1;

            /*texture = new Texture(canvas->context, {
                .data = bitmap.data,
                .width = bitmap.width, .height = bitmap.height,
                .channels = 4
            });*/
        }

        ~Svg() {

            if (bitmap.data) { delete[] bitmap.data; }
            if (texture) { delete texture; }
        }

        void bake() {

            dbg("[Svg] Baking");

            // Free old resources if rebaking
            if (texture) { delete texture; texture = nullptr; }
            if (bitmap.data) { delete[] bitmap.data; bitmap.data = nullptr; }
            if (rast) { nsvgDeleteRasterizer(rast); rast = nullptr; }
            if (image) { nsvgDelete(image); image = nullptr; }

            // Ensure resource is valid
            if (!resource.data || resource.size == 0) {
                throw std::runtime_error("Svg::bake(): resource is empty.");
            }

            // SVG text must be null-terminated for NanoSVG
            std::string svgText((char*)resource.data, resource.size);

            // Parse SVG from memory
            image = nsvgParse(
                (char*)svgText.c_str(),
                "px",   // units
                96.0f   // DPI
            );

            if (!image) {
                throw std::runtime_error("Svg::bake(): failed to parse SVG.");
            }

            if (width <= 0 || height <= 0) {
                throw std::runtime_error("Svg::bake(): invalid SVG dimensions.");
            }

            // Allocate pixel buffer (RGBA 8-bit)
            bitmap.width = width; bitmap.height = height;
            bitmap.size = bitmap.width * bitmap.height * 4 * sizeof(char);
            bitmap.data = new unsigned char[bitmap.size];
            std::memset(bitmap.data, 0, bitmap.size);

            // Create rasterizer
            rast = nsvgCreateRasterizer();
            if (!rast) {
                throw std::runtime_error("Svg::bake(): failed to create rasterizer.");
            }

            // NanoSVG only supports UNIFORM scale → choose one
            scale = std::fmin(width / image->width, height / image->height);

            // Rasterize!
            nsvgRasterize(
                rast,
                image,
                0, 0,      // no translation
                scale,     // scaling applied to rasterizer
                bitmap.data,
                bitmap.width,
                bitmap.height,
                bitmap.width * 4  // stride
            );

            // Upload to GPU texture
            texture = new Texture(canvas->context, {
                .data = bitmap.data,
                .width  = bitmap.width,
                .height = bitmap.height,
                .channels = 4,
                .filter = Texture::Filter::Bilinear
            });
        }
    };
};