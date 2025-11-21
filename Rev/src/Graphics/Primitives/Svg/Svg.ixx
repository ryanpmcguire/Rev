module;

#include <cstddef>
#include <managed.hpp>

export module Rev.Primitive.Svg;

import Rev.Primitive;
import Rev.Core.Resource;
import Rev.Core.Shared;
import Rev.Core.Pos;
import Rev.Core.Rect;
import Rev.Core.Color;
import Rev.Core.Vertex;
import Rev.Core.Svg;

// Rev graphics modules
import Rev.Graphics.Canvas;
import Rev.Graphics.UniformBuffer;
import Rev.Graphics.VertexBuffer;
import Rev.Graphics.Pipeline;
import Rev.Graphics.Shader;

export namespace Rev::Primitive {

    struct Svg : public Primitive {

        // Shared
        //--------------------------------------------------

        inline static Shared shared;
        inline static Pipeline* pipeline = nullptr;
        inline static VertexBuffer* vertices = nullptr;

        void createShared() {

            // Color pipeline
            pipeline = new Pipeline(canvas->context, {

                .attribs = Vertex::attribs,

                .openGlVert = File("./Shaders/Svg.vert"),
                .openGlFrag = File("./Shaders/Svg.frag"),
                .metalUniversal = File("./Shaders/Svg.metal")
            });

            vertices = new VertexBuffer(canvas->context, { .num = 6, .divisor = 1, .attribs = Vertex::attribs });
        }

        void destroyShared() {

            delete pipeline;
            delete vertices;
        }

        // Instance
        //--------------------------------------------------

        // Instance-specific data
        struct Data {

            struct Corners { float tl, tr, bl, br; };            
            struct BorderWidth { float l, r, t, b; };
            struct BorderColor { Core::Color l, r, t, b; };
            struct Shadow { float x, y, size, blur; Core::Color color; };

            Core::Rect rect;
            Core::Color color;
            Corners corners;

            BorderWidth borderWidth;
            BorderColor borderColor;

            Shadow shadow;
        };

        Core::Svg* svg = nullptr;
        UniformBuffer* databuff = nullptr;
        Data* data = nullptr;

        Resource resource;

        // Create
        Svg(Canvas* canvas) : Primitive(canvas) {

            shared.create([this]() { this->createShared(); });

            //vertices = new VertexBuffer(4);
            svg = new Core::Svg(canvas, resource);
            databuff = new UniformBuffer(canvas->context, sizeof(Data));
            data = static_cast<Data*>(databuff->data);

            *data = {

                .rect = { 100, 100, 100, 100 },
                .color = { 1, 1, 1, 1 },

                .corners = { 5, 10, 15, 25 },

                .borderWidth = { 0, 0, 0, 0 },
                .borderColor = {
                    { 1, 1, 1, 1 },
                    { 1, 1, 1, 1 },
                    { 1, 1, 1, 1 },
                    { 1, 1, 1, 1 }
                },

                .shadow = {
                    .x = 0, .y = 0,
                    .size = 10, .blur = 10,
                    .color = { 1, 1, 1, 0 }
                }
            };
        }

        // Destroy
        ~Svg() {

            shared.destroy([this]() { this->destroyShared(); });
            
            delete databuff;
        }

        void compute() override {

            // If no change, do nothing
            if (svg->resource == resource &&
                svg->width == data->rect.w &&
                svg->height == data->rect.h) {
                return;
            }
            
            svg->resource = resource;
            svg->width = data->rect.w;
            svg->height = data->rect.h;
            
            svg->bake();
        }

        // Draw color
        void draw() override {
         
            pipeline->bind();
            vertices->bind();
            databuff->bind(1);
            svg->texture->bind(0);

            canvas->drawArraysInstanced(Pipeline::Topology::TriangleList, 0, 6, 1);
        }
    };
};