module;

#include <vector>
#include <glew/glew.h>

export module Rev.Graphics.Triangles;

import Rev.Graphics.Canvas;
import Rev.Graphics.Primitive;
import Rev.Graphics.VertexBuffer;
import Rev.Graphics.Pipeline;
import Rev.Graphics.Shader;
import Resources.Shaders.OpenGL.Triangles.Triangles_vert;
import Resources.Shaders.OpenGL.Triangles.Triangles_frag;

export namespace Rev {

    struct Triangles : public Primitive {

        // Shared across instances
        struct Shared {

            size_t refCount = 0;

            Pipeline* pipeline = nullptr;

            Shared() {
                
            }

            void create(Canvas* canvas) {

                refCount++;

                if (refCount > 1) { return; }

                // Create resources
                pipeline = new Pipeline(canvas->context, {});
            }

            void destroy() {

                // Subtract refcount, return if remaining
                if (refCount--) { return; }

                // Delete resources
                delete pipeline;
            }
        };

        inline static Shared shared;
        VertexBuffer* vertices = nullptr;
        bool dirty = true;

        // Create
        Triangles(Canvas* canvas) : Primitive(canvas) {

            // If we're first
            shared.create(canvas);

            vertices = new VertexBuffer(canvas->context, 3);

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

        void draw(Canvas* canvas) override {

            if (dirty) {
                this->compute();
                dirty = false;
            }

            shared.pipeline->bind();
            vertices->bind();
            canvas->drawArrays(Pipeline::Topology::TriangleList, 0, 3);
        }
    };
};