module;

#include <glew/glew.h>

export module Rev.OpenGL.Triangles;

import Rev.OpenGL.VertexBuffer;
import Rev.OpenGL.Pipeline;
import Rev.OpenGL.Shader;
import Resources.Shaders.OpenGL.Triangles.Triangles_vert;
import Resources.Shaders.OpenGL.Triangles.Triangles_frag;

export namespace Rev {

    export struct Triangles {

        VertexBuffer* vb = nullptr;
        Shader* vert = nullptr;
        Shader* frag = nullptr;
        Pipeline* pipeline = nullptr;

        // Create
        Triangles() {

            vb = new VertexBuffer(1);
            vert = new Shader(Triangles_vert, GL_VERTEX_SHADER);
            frag = new Shader(Triangles_frag, GL_FRAGMENT_SHADER);
            pipeline = new Pipeline(*vert, *frag);
        }

        // Destroy
        ~Triangles() {

            delete vb;
            delete vert;
            delete frag;
            delete pipeline;
        }

        void draw() {

        }
    };
};