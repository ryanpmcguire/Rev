module;

#include <string>
#include <stdexcept>
#include <vector>

#include <glew/glew.h>
#include <dbg.hpp>

export module Rev.Graphics.Pipeline;

import Rev.Core.Resource;
import Rev.Graphics.Shader;

export namespace Rev::Graphics {

    using namespace Rev::Core;

    struct Pipeline {

        enum Topology {
            TriangleFan = GL_TRIANGLE_FAN,
            TriangleList = GL_TRIANGLES
        };

        GLuint id = 0;
        size_t users = 0;

        Shader* vert = nullptr;
        Shader* frag = nullptr;

        struct Params {

            bool instanced = true;
            std::vector<float> attribs;

            Resource openGlVert;
            Resource openGlFrag;

            Resource metalUniversal;

            Resource vulkanVert;
            Resource vulkanFrag;
        };

        // Create
        Pipeline(void* context, Params params) {
            
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