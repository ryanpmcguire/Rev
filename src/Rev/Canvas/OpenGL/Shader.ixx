module;

#include <string>
#include <stdexcept>
#include <glew/glew.h>

export module Rev.OpenGL.Shader;

import Resource;

export namespace Rev {

    struct Shader {

        GLuint shader = 0;

        // Create
        Shader(Resource shaderFile, GLenum shaderType) {

            const char* srcStr = reinterpret_cast<const char*>(shaderFile.data);

            shader = glCreateShader(shaderType);
            glShaderSource(shader, 1, &srcStr, nullptr);
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