module;

#include <vector>
#include <glew/glew.h>

export module Rev.OpenGL.Lines;

import Rev.OpenGL.Primitive;
import Rev.OpenGL.VertexBuffer;
import Rev.OpenGL.Pipeline;
import Rev.OpenGL.Shader;
import Resources.Shaders.OpenGL.Triangles.Triangles_vert;
import Resources.Shaders.OpenGL.Triangles.Triangles_frag;

export namespace Rev {

    export struct Triangles : public Primitive {

        // Shared across instances
        struct Shared {

            size_t refCount = 0;

            Shader* vert = nullptr;
            Shader* frag = nullptr;
            Pipeline* pipeline = nullptr;

            void create() {

                refCount++;

                // Create resources
                vert = new Shader(Triangles_vert, GL_VERTEX_SHADER);
                frag = new Shader(Triangles_frag, GL_FRAGMENT_SHADER);
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
        VertexBuffer* vertices = nullptr;
        bool dirty = true;

        // Create
        Triangles() {

            // If we're first
            shared.create();

            vertices = new VertexBuffer(3);

            vertices->set({
                {  0,  0 },  // top
                { 10, 10 },  // bottom left
                {  0, 10 }   // bottom right
            });
        }

        // Destroy
        ~Triangles() {

            // If we're last
            shared.destroy();

            delete vertices;
        }

        void compute() {
            
        }

        void draw() override {

            if (dirty) {
                this->compute();
                dirty = false;
            }

            shared.pipeline->bind();
            vertices->bind();
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
    };
};