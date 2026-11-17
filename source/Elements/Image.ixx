module;

#include <cmath>
#include <string>

#define STB_IMAGE_IMPLEMENTATION      // the one unit that compiles stb_image
#include <stb/StbImage.h>

export module Rev.Element.Image;

import Rev.Core.Rect;
import Rev.Core.Color;
import Rev.Core.Resource;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;

import Rev.Graphics.Canvas;
import Rev.Graphics.Texture;
import Rev.Primitive.Svg;

export namespace Rev::Element {

    using namespace Rev::Appearance;

    // Image: a static bitmap (anything stb_image decodes), aspect-fit and centred in its rect.
    // Decoded once, at construction, and drawn through the Svg primitive's textured quad.
    struct Image : public Element {

        Primitives::Svg* quad = nullptr;            // the Svg primitive, used only as a textured quad
        Rev::Graphics::Texture* texture = nullptr;  // the decoded RGBA pixels on the GPU
        Resource resource;

        int width = 0;          // decoded pixel size; 0 when decoding failed
        int height = 0;
        std::string problem;    // why the image is empty, if it is

        float rotation = 0.0f;
        float opacity = 1.0f;

        // Create
        Image(Element* parent, Resource resource, StyleList styles = {}, std::string name = "Image") : Element(parent, styles, name) {

            this->resource = resource;
            quad = new Primitives::Svg(shared->canvas);

            if (!resource.data || resource.size == 0) {
                problem = "empty resource";
                return;
            }

            int channels = 0;
            unsigned char* pixels = stbi_load_from_memory((const stbi_uc*)resource.data, (int)resource.size, &width, &height, &channels, 4);

            if (!pixels) {
                problem = stbi_failure_reason();
                width = 0;
                height = 0;
                return;
            }

            texture = new Rev::Graphics::Texture(shared->canvas->context, {
                .data = pixels,
                .width = (size_t)width,
                .height = (size_t)height,
                .channels = 4,
                .filter = Rev::Graphics::Texture::Filter::Bilinear
            });

            stbi_image_free(pixels);

            quad->svg->texture = texture;   // the quad draws our texture; we keep ownership
        }

        // Destroy
        ~Image() {
            quad->svg->texture = nullptr;
            delete texture;
            delete quad;
        }

        void computePrimitives(Event& e) override {

            if (resolved.hidden) { return; }
            if (!texture) { return; }

            const Rect bounds = this->rect.rounded();

            if (bounds.w <= 0.0f || bounds.h <= 0.0f) { return; }

            // Aspect-fit and centre the bitmap within the box (as Core::Svg does for its artwork)
            float s = std::fmin(bounds.w / width, bounds.h / height);
            float w = width * s;
            float h = height * s;

            Primitives::Svg::Data& data = *quad->data;

            data.rect = Rect{ bounds.x + (bounds.w - w) * 0.5f, bounds.y + (bounds.h - h) * 0.5f, w, h }.rounded();
            data.color = { 0, 0, 0, 0 };    // zero colour: the shader keeps the bitmap's own colours
            data.rotation = rotation;
            data.opacity = opacity * resolved.opacity;

            // No quad->compute(): that would re-bake the resource as SVG.

            Element::computePrimitives(e);
        }

        // Draw own rect
        void draw(Event& e) override {

            if (!resolved.hidden) {
                quad->draw();
            }

            Element::draw(e);
        }
    };
};
