module;

#include <string>
#include <stdexcept>

#include "./Helpers/MetalBackend.hpp"

export module Rev.Metal.Shader;

import Resource;

export namespace Rev {

    struct Shader {

        enum Stage {
            Vertex,
            Fragment
        };

        //GLuint shader = 0;

        void* shader = nullptr;

        // Create
        Shader(void* context, Resource shaderFile, Stage shaderType) {

            const char* srcStr = reinterpret_cast<const char*>(shaderFile.data);

            shader = metal_create_shader(
                (MetalContext*)context,
                reinterpret_cast<const char*>(shaderFile.data),
                shaderFile.size,
                (MetalShaderStage)shaderType
            );

            

            /*shader = glCreateShader(shaderType);

            glShaderSource(shader, 1, &srcStr, &srcLen);

            return;
            glCompileShader(shader);

            // Check for errors
            GLint success;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

            if (!success) {

                char infoLog[512];
                glGetShaderInfoLog(shader, 512, nullptr, infoLog);

                throw std::runtime_error(std::string("Shader compilation failed: ") + infoLog);
            }*/
        }

        // Destroy
        ~Shader() {

            if (shader) {
                metal_destroy_shader(shader);
                shader = nullptr;
            }

            // Shader no longer needed after linking
            //glDeleteShader(shader);
        }
    };
};