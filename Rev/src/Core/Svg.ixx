module;

#include <stdexcept>

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvg/nanosvg.h>
#include <nanosvg/nanosvgrast.h>

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
        int width = 0;
        int height = 0;
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

            texture = new Texture(canvas->context, {
                .data = bitmap.data,
                .width = bitmap.width, .height = bitmap.height,
                .channels = 1
            });
        }

        ~Svg() {

            if (bitmap.data) { delete[] bitmap.data; }
            if (texture) { delete texture; }
        }

        void bake() {

            // Free old resources if rebaking
            if (texture) { delete texture; texture = nullptr; }
            if (pixels) { delete[] pixels; pixels = nullptr; }
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

            // Compute scaled raster size
            width  = int(image->width  * scale);
            height = int(image->height * scale);

            if (width <= 0 || height <= 0) {
                throw std::runtime_error("Svg::bake(): invalid SVG dimensions.");
            }

            // Allocate pixel buffer (RGBA 8-bit)
            pixels = new unsigned char[width * height * 4];
            std::memset(pixels, 0, width * height * 4);

            // Create rasterizer
            rast = nsvgCreateRasterizer();
            if (!rast) {
                throw std::runtime_error("Svg::bake(): failed to create rasterizer.");
            }

            // Rasterize!
            nsvgRasterize(
                rast,
                image,
                0, 0,      // no translation
                scale,     // scaling applied to rasterizer
                pixels,
                width,
                height,
                width * 4  // stride
            );

            // Upload to GPU texture
            texture = new Rev::Graphics::Texture(canvas->context, {
                .data = pixels,
                .width  = (size_t)width,
                .height = (size_t)height,
                .channels = 4
            });
        }

    };
};