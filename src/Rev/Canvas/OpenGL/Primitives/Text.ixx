module;

#include <cmath>
#include <vector>
#include <string>
#include <glew/glew.h>

export module Rev.OpenGL.Text;

import Rev.OpenGL.Primitive;
import Rev.OpenGL.UniformBuffer;
import Rev.OpenGL.VertexBuffer;
import Rev.OpenGL.Texture;
import Rev.OpenGL.Pipeline;
import Rev.OpenGL.Shader;
import Resources.Shaders.OpenGL.Text.Text_vert;
import Resources.Shaders.OpenGL.Text.Text_frag;

import Rev.Font;

export namespace Rev {

    export struct Text : public Primitive {

        // Shared across instances
        struct Shared {

            size_t refCount = 0;

            Shader* vert = nullptr;
            Shader* frag = nullptr;
            Pipeline* pipeline = nullptr;

            void create() {

                refCount++;

                // Create resources
                vert = new Shader(Text_vert, GL_VERTEX_SHADER);
                frag = new Shader(Text_frag, GL_FRAGMENT_SHADER);
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

        inline static Shared shared;
        
        // Instance-specific data
        struct Data {

            struct Color { float r, g, b, a; };

            Color color;
        };

        UniformBuffer* databuff = nullptr;
        Font* font = nullptr;
        Texture* texture = nullptr;

        struct CharVertex {
            float x, y;     // Screen coords
            float u, v;     // Texture coords
        };

        VertexBuffer* vertices = nullptr;
        size_t vertexCount = 0;

        Data* data = nullptr;
        bool dirty = true;

        std::string content = "HELLO WORLD";

        // Create
        Text() {

            shared.create();

            font = new Font();
            texture = new Texture(font->bitmap.data, font->bitmap.width, font->bitmap.height, 1);
            vertices = new VertexBuffer(250, sizeof(CharVertex));
            
            databuff = new UniformBuffer(sizeof(Data));
            data = static_cast<Data*>(databuff->data);

            //texture->fill(1.0f);

            *data = {
                .color = { 1, 1, 1, 1 }
            };
        }

        // Destroy
        ~Text() {

            shared.destroy();

            delete vertices;
            delete font;
        }

        float xPos = 0;
        float yPos = 0;

        // Compute vertices
        void compute() {

            CharVertex* verts = (CharVertex*)vertices->data;
            size_t max = vertices->num; // total available vertex slots
            size_t count = 0;

            float x = xPos;
            float y = yPos + font->ascentPx;

            for (char c : content) {
                if (c < 32 || c >= 128) continue;

                // Ensure space for 6 vertices per character
                if (count + 6 > max) break;

                Font::Quad q = font->getQuad(c, x, y);

                // Triangle 1
                verts[count++] = { q.x0, q.y0, q.s0, q.t0 };
                verts[count++] = { q.x1, q.y0, q.s1, q.t0 };
                verts[count++] = { q.x1, q.y1, q.s1, q.t1 };

                // Triangle 2
                verts[count++] = { q.x0, q.y0, q.s0, q.t0 };
                verts[count++] = { q.x1, q.y1, q.s1, q.t1 };
                verts[count++] = { q.x0, q.y1, q.s0, q.t1 };
            }

            vertexCount = count;
        }
                
        // Draw vertices
        void draw() override {

            if (dirty) {
                this->compute();
            }
        
            shared.pipeline->bind();
            texture->bind(0);  // bind to GL_TEXTURE0
            vertices->bind();
            databuff->bind(1);

            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertexCount));
        }
    };
};