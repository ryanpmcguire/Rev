module;

#include <vector>
#include <glew/glew.h>

export module Rev.OpenGL.Rectangle;

import Rev.OpenGL.Primitive;
import Rev.OpenGL.UniformBuffer;
import Rev.OpenGL.VertexBuffer;
import Rev.OpenGL.Pipeline;
import Rev.OpenGL.Shader;
import Resources.Shaders.OpenGL.Rectangle.Rectangle_vert;
import Resources.Shaders.OpenGL.Rectangle.Rectangle_frag;

export namespace Rev {

    export struct Rectangle : public Primitive {

        // Shared across instances
        struct Shared {

            size_t refCount = 0;

            Shader* vert = nullptr;
            Shader* frag = nullptr;
            Pipeline* pipeline = nullptr;

            void create() {

                refCount++;

                // Create resources
                vert = new Shader(Rectangle_vert, GL_VERTEX_SHADER);
                frag = new Shader(Rectangle_frag, GL_FRAGMENT_SHADER);
                pipeline = new Pipeline(*(vert), *(frag));
            }

            void destroy() {

                // Subtract refcount, return if remaining
                if (refCount--) { return; }

                // Delete resources
                delete pipeline;
                delete vert;
                delete frag;
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

        VertexBuffer* vertices = nullptr;
        UniformBuffer* databuff = nullptr;

        Data* data = nullptr;
        bool dirty = true;

        // Create
        Rectangle() {

            shared.create();

            vertices = new VertexBuffer(4);
            databuff = new UniformBuffer(sizeof(Data));
            
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

            delete vertices;
            delete databuff;
        }

        void compute() {

            Data& data = (*this->data);

            float& l = data.rect.x;
            float& t = data.rect.y;
            float r = l + data.rect.w;
            float b = t + data.rect.h;

            vertices->set({
                {l, t}, {r, t},
                {r, b}, {l, b}
            });
        }

        void draw() override {

            if (dirty) {
                this->compute();
                dirty = false;
            }
         
            shared.pipeline->bind();
            vertices->bind();
            databuff->bind(1);

            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        }
    };
};