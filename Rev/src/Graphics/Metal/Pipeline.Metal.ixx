module;

#include <string>
#include <stdexcept>
#include <dbg.hpp>

#include "./Helpers/MetalBackend.hpp"

export module Rev.Metal.Pipeline;

import Rev.Metal.Shader;

export namespace Rev {

    struct Pipeline {

        enum Topology {
            TriangleList,
            TriangleFan
        };

        void* pipeline = nullptr;

        //GLuint id = 0;

        // Create
        Pipeline(void* context, Shader* vert, Shader* frag) {

            pipeline = metal_create_pipeline((MetalContext*)context, vert->shader, frag->shader);
            
            /*id = glCreateProgram();
            glAttachShader(id, vert.shader);
            glAttachShader(id, frag.shader);
            glLinkProgram(id);

            GLint success;
            glGetProgramiv(id, GL_LINK_STATUS, &success);*/

            /*if (!success) {

                char infoLog[512];
                glGetProgramInfoLog(id, 512, nullptr, infoLog);
                
                throw std::runtime_error(std::string("Pipeline link error: ") + infoLog);
            }*/
        }

        // Destroy
        ~Pipeline() {

            if (pipeline) {
                metal_destroy_pipeline(pipeline);
                pipeline = nullptr;
            }
        }

        void bind(void* context) {
            metal_bind_pipeline((MetalContext*)context, pipeline);
        }
    };
};