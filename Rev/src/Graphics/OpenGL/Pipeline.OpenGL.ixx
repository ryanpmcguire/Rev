module;

#include <string>
#include <stdexcept>
#include <dbg.hpp>

export module Rev.OpenGL.Pipeline;

import Rev.OpenGL.Shader;

export namespace Rev {

    struct Pipeline {

        enum Topology {
            TriangleFan = GL_TRIANGLE_FAN,
            TriangleList = GL_TRIANGLES
        };

        GLuint id = 0;

        // Create
        Pipeline(Shader& vert, Shader& frag) {
            
            id = glCreateProgram();
            glAttachShader(id, vert.shader);
            glAttachShader(id, frag.shader);
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

            if (id) {
                glDeleteProgram(id);
            }
        }

        void bind() {
            glUseProgram(id);
        }
    };
};