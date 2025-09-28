module;

#include <vector>
//#include <glew/glew.h>

export module Rev.Graphics.Rectangle;

import Rev.Graphics.Canvas;
import Rev.Graphics.Primitive;
import Rev.Graphics.UniformBuffer;
import Rev.Graphics.VertexBuffer;
import Rev.Graphics.Pipeline;
import Rev.Graphics.Pipelines.RectanglePipeline;
import Rev.Graphics.Shader;

export namespace Rev {

    struct Rectangle : public Primitive {

        // Shared across instances
        struct Shared {

            size_t refCount = 0;

            RectanglePipeline* pipeline = nullptr;
            VertexBuffer* vertices = nullptr;

            Shared() {
                
            }

            void create(Canvas* canvas) {

                refCount++;

                if (refCount > 1) { return; }

                // Create resources
                
                pipeline = new RectanglePipeline(canvas->context);
                vertices = new VertexBuffer(canvas->context, 6, 2, 1);
            }

            void destroy() {

                // Subtract refcount, return if remaining
                if (refCount--) { return; }

                // Delete resources
                delete vertices;
                delete pipeline;
            }
        };

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

        //VertexBuffer* vertices = nullptr;
        UniformBuffer* databuff = nullptr;

        Data* data = nullptr;
        bool dirty = true;

        // Create
        Rectangle(Canvas* canvas) : Primitive(canvas) {

            shared.create(canvas);

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

            shared.destroy();

            //delete vertices;
            delete databuff;
        }

        void compute() {

            Data& data = (*this->data);

            /*float& l = data.rect.x;
            float& t = data.rect.y;
            float r = l + data.rect.w;
            float b = t + data.rect.h;

            vertices->set({
                {l, t}, {r, t}, {l, b}, 
                {l, b}, {r, t}, {r, b}
            });*/
        }

        void draw(Canvas* canvas) override {

            //compute();
         
            shared.pipeline->bind(canvas->context);
            shared.vertices->bind(canvas->context);
            databuff->bind(canvas->context, 1);

            canvas->drawArraysInstanced(Pipeline::Topology::TriangleList, 0, 6, 1);
        }
    };
};