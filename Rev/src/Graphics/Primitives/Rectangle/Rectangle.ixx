module;

#include <cstddef>

export module Rev.Primitive.Rectangle;

import Rev.Primitive;
import Rev.Core.Shared;
import Rev.Core.Pos;

// Rev graphics modules
import Rev.Graphics.Canvas;
import Rev.Graphics.UniformBuffer;
import Rev.Graphics.VertexBuffer;
import Rev.Graphics.Pipeline;
import Rev.Graphics.Shader;

// Shader resources
import Resources.Shaders.Metal.Rectangle.Rectangle_metal;
import Resources.Shaders.Metal.Rectangle.RectangleStencil_metal;
import Resources.Shaders.OpenGL.Rectangle.Rectangle_vert;
import Resources.Shaders.OpenGL.Rectangle.Rectangle_frag;

export namespace Rev::Primitive {

    struct Rectangle : public Primitive {

        // Instance-specific data
        struct Data {

            struct Rect { float x, y, w, h; };
            struct Color { float r, g, b, a; };
            struct Corners { float tl, tr, bl, br; };

            Rect rect;
            Color color;
            Corners corners;
        };

        inline static Shared shared;
        inline static Pipeline* pipeline = nullptr;
        inline static Pipeline* stencilPipeline = nullptr;
        inline static VertexBuffer* vertices = nullptr;

        UniformBuffer* databuff = nullptr;
        Data* data = nullptr;

        // Create
        Rectangle(Canvas* canvas) : Primitive(canvas) {

            shared.create([canvas]() {

                // Color pipeline
                pipeline = new Pipeline(canvas->context, {

                    .attribs = { 2, 4 },

                    .openGlVert = Rectangle_vert,
                    .openGlFrag = Rectangle_frag,
                    .metalUniversal = Rectangle_metal
                });

                stencilPipeline = new Pipeline(canvas->context, {

                    .attribs = { 2, 4 },

                    .openGlVert = Rectangle_vert,
                    .openGlFrag = Rectangle_frag,
                    .metalUniversal = RectangleStencil_metal
                });

                vertices = new VertexBuffer(canvas->context, { .num = 6, .divisor = 1, .attribs = { 2, 4 } });
            });

            //vertices = new VertexBuffer(4);
            databuff = new UniformBuffer(canvas->context, sizeof(Data));
            data = static_cast<Data*>(databuff->data);

            *data = {
                .rect = { .x = 100, .y = 100, .w = 100, .h = 100 },
                .color = { .r = 1, .g = 1, .b = 1, .a = 1 },
                .corners = { 5, 10, 15, 25 }
            };
        }

        // Destroy
        ~Rectangle() {

            shared.destroy([]() {
                delete pipeline;
                delete stencilPipeline;
                delete vertices;
            });
            
            delete databuff;
        }

        void compute() override {
            Data& data = (*this->data);
        }

        // Draw stencil
        void stencil() {

            stencilPipeline->bind();
            vertices->bind();
            databuff->bind(1);

            canvas->drawArraysInstanced(Pipeline::Topology::TriangleList, 0, 6, 1);
        }

        // Draw color
        void draw() override {
         
            pipeline->bind();
            vertices->bind();
            databuff->bind(1);

            canvas->drawArraysInstanced(Pipeline::Topology::TriangleList, 0, 6, 1);
        }
    };
};