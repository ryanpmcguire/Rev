module;

#include <cstddef>
#include <managed.hpp>

export module Rev.Primitive.Image;

import Rev.Primitive;
import Rev.Core.Resource;
import Rev.Core.Shared;
import Rev.Core.Rect;
import Rev.Core.Vertex;

import Rev.Graphics.Canvas;
import Rev.Graphics.UniformBuffer;
import Rev.Graphics.VertexBuffer;
import Rev.Graphics.Pipeline;
import Rev.Graphics.Shader;
import Rev.Graphics.Texture;

export namespace Rev::Primitives {

    using namespace Rev::Core;
    using namespace Rev::Graphics;

    struct Image : public Primitive {

        // Shared pipeline (one per process, ref-counted via Shared)
        //--------------------------------------------------

        inline static Shared shared;
        inline static Pipeline* pipeline  = nullptr;
        inline static VertexBuffer* vertices = nullptr;

        void createShared() {
            pipeline = new Pipeline(canvas->context, {
                .attribs       = Vertex::attribs,
                .openGlVert    = File("./Shaders/Image.vert"),
                .openGlFrag    = File("./Shaders/Image.frag"),
            });
            vertices = new VertexBuffer(canvas->context, {
                .num     = 6,
                .divisor = 1,
                .attribs = Vertex::attribs
            });
        }

        void destroyShared() {
            delete pipeline;
            delete vertices;
        }

        // Instance data uploaded to GPU via uniform buffer
        //--------------------------------------------------

        struct Data {
            float x, y, w, h;      // screen-space rect (letterboxed inside element)
            float opacity;
            float tileCountX;       // horizontal tile count; 0 = no grid
            float tileCountY;       // vertical tile count;   0 = no grid
            float pad;              // pad to 32-byte std140 alignment
        };

        UniformBuffer* databuff = nullptr;
        Data*          data     = nullptr;

        // Texture owned externally (by ImagePreview element)
        Texture* texture = nullptr;

        // Create
        Image(Canvas* canvas) : Primitive(canvas) {

            shared.create([this]() { this->createShared(); });

            databuff = new UniformBuffer(canvas->context, sizeof(Data));
            data = static_cast<Data*>(databuff->data);

            *data = { 0, 0, 100, 100, 1.0f, 0, 0, 0 };
        }

        // Destroy
        ~Image() {
            shared.destroy([this]() { this->destroyShared(); });
            delete databuff;
            // texture is owned by ImagePreview — do not delete here
        }

        void compute() override {}

        void draw() override {
            if (!texture) return;
            pipeline->bind();
            vertices->bind();
            databuff->bind(1);
            texture->bind(0);
            canvas->drawArraysInstanced(Pipeline::Topology::TriangleList, 0, 6, 1);
        }
    };
}
