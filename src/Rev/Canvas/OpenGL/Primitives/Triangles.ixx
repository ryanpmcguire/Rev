module;

#include <vector>
#include <glew/glew.h>

export module Rev.OpenGL.Triangles;

import Rev.OpenGL.Primitive;
import Rev.OpenGL.VertexBuffer;
import Rev.OpenGL.Pipeline;
import Rev.OpenGL.Shader;
import Resources.Shaders.OpenGL.Triangles.Triangles_vert;
import Resources.Shaders.OpenGL.Triangles.Triangles_frag;

export namespace Rev {

    export struct Triangles : public Primitive {

        Shader* vert = nullptr;
        Shader* frag = nullptr;
        Pipeline* pipeline = nullptr;
        VertexBuffer* vertices = nullptr;

        // Create
        Triangles() {

            vert = new Shader(Triangles_vert, GL_VERTEX_SHADER);
            frag = new Shader(Triangles_frag, GL_FRAGMENT_SHADER);
            pipeline = new Pipeline(*vert, *frag);
            vertices = new VertexBuffer(3);

            vertices->set({
                {  0,  0 },  // top
                { 10, 10 },  // bottom left
                {  0, 10 }   // bottom right
            });
        }

        // Destroy
        ~Triangles() {

            delete vert;
            delete frag;
            delete pipeline;
            delete vertices;
        }

        void draw() override {

            pipeline->bind();
            vertices->bind();
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
    };
};