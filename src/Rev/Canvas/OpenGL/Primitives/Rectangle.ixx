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

        struct Data {

            struct Rect { float x, y, w, h; };
            struct Color { float r, g, b, a; };
            struct Corners { float tl, tr, bl, br; };

            Rect rect;
            Color color;
            Corners corners;
        };

        Shader* vert = nullptr;
        Shader* frag = nullptr;
        Pipeline* pipeline = nullptr;
        VertexBuffer* vertices = nullptr;
        UniformBuffer* databuff = nullptr;

        Data* data = nullptr;
        bool dirty = true;

        // Create
        Rectangle() {

            vert = new Shader(Rectangle_vert, GL_VERTEX_SHADER);
            frag = new Shader(Rectangle_frag, GL_FRAGMENT_SHADER);
            pipeline = new Pipeline(*vert, *frag);
            vertices = new VertexBuffer(6);
            databuff = new UniformBuffer(sizeof(Data));
            
            data = static_cast<Data*>(databuff->data);

            *data = {
                .rect = { .x = 100, .y = 100, .w = 100, .h = 100 },
                .color = { .r = 1, .g = 1, .b = 0, .a = 1 },
                .corners = { 10, 10, 10, 10 }
            };
        }

        // Destroy
        ~Rectangle() {

            delete vert;
            delete frag;
            delete pipeline;
            delete vertices;
        }

        void draw() override {

            if (dirty) {

                Data& data = (*this->data);

                float& l = data.rect.x;
                float& t = data.rect.y;
                float r = l + data.rect.w;
                float b = t + data.rect.h;

                vertices->set({
                    {t, l}, {t, r}, {b, l},
                    {b, l}, {t, r}, {b, r}
                });
            }
         
            pipeline->bind();
            vertices->bind();
            databuff->bind(1);

            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
    };
};