module;

#include <string>
#include <stdexcept>

#include <glew/glew.h>
#include <dbg.hpp>

export module Rev.OpenGL.Pipeline;

import Resource;
import Rev.OpenGL.Shader;

export namespace Rev {

    struct Pipeline {

        enum Topology {
            TriangleFan = GL_TRIANGLE_FAN,
            TriangleList = GL_TRIANGLES
        };

        GLuint id = 0;

        Shader* vert = nullptr;
        Shader* frag = nullptr;

        struct PipelineParams {

            Resource openGlVert;
            Resource openGlFrag;

            Resource metalUniversal;

            Resource vulkanVert;
            Resource vulkanFrag;
        };

        // Create
        Pipeline(void* context, PipelineParams params, int floatsPerVertex = 0, bool instanced = true) {
            
            vert = new Shader(params.openGlVert, Shader::Stage::Vertex);
            frag = new Shader(params.openGlFrag, Shader::Stage::Fragment);

            id = glCreateProgram();
            glAttachShader(id, vert->shader);
            glAttachShader(id, frag->shader);
            glLinkProgram(id);

            GLint success;
            glGetProgramiv(id, GL_LINK_STATUS, &success);

            if (!success) {

                char infoLog[512];
                glGetProgramInfoLog(id, 512, nullptr, infoLog);
                
                throw std::runtime_error(std::string("Pipeline link error: ") + infoLog);
            }
        }

        // Destroy
        ~Pipeline() {

            delete vert;
            delete frag;

            if (id) {
                glDeleteProgram(id);
            }
        }

        void bind() {
            glUseProgram(id);
        }
    };
};