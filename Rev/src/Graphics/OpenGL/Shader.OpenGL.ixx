module;

#include <string>
#include <stdexcept>
#include <glew/glew.h>

export module Rev.Graphics.Shader;

import Rev.Core.Resource;

export namespace Rev::Graphics {

    using namespace Rev::Core;

    struct Shader {

        enum Stage {
            Vertex = GL_VERTEX_SHADER,
            Fragment = GL_FRAGMENT_SHADER
        };

        GLuint shader = 0;

        // Create
        Shader(Resource shaderFile, Stage shaderType) {

            const char* srcStr = reinterpret_cast<const char*>(shaderFile.data);
            GLint srcLen = static_cast<GLint>(shaderFile.size);

            shader = glCreateShader(shaderType);

            glShaderSource(shader, 1, &srcStr, &srcLen);
            glCompileShader(shader);

            // Check for errors
            GLint success;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

            if (!success) {

                char infoLog[512];
                glGetShaderInfoLog(shader, 512, nullptr, infoLog);

                throw std::runtime_error(std::string("Shader compilation failed: ") + infoLog);
            }
        }

        // Destroy
        ~Shader() {

            // Shader no longer needed after linking
            glDeleteShader(shader);
        }
    };
};