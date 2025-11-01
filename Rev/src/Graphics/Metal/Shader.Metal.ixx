module;

#include <string>
#include <stdexcept>
#include <glew/glew.h>
#include <dbg.hpp>

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

        Shader(Resource shaderFile, Stage shaderType, const std::string& defines = "") {

            const char* srcStr = reinterpret_cast<const char*>(shaderFile.data);
            std::string src(srcStr, shaderFile.size);

            // Look for placeholder
            const std::string marker = "DEFINITIONS";
            size_t pos = src.find(marker);

            if (pos != std::string::npos) {
                // Replace marker with defines (add newline if needed)
                std::string injected = defines;
                if (!injected.empty() && injected.back() != '\n') { injected += '\n'; }
                src.replace(pos, marker.length(), injected);
            }

            // Prepare for GL
            const char* finalSrc = src.c_str();
            GLint finalLen = static_cast<GLint>(src.size());

            // Create and compile shader
            shader = glCreateShader(shaderType);
            glShaderSource(shader, 1, &finalSrc, &finalLen);
            glCompileShader(shader);

            // Error check
            GLint success;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

            if (!success) {
                char infoLog[512];
                glGetShaderInfoLog(shader, 512, nullptr, infoLog);
                dbg("Shader compilation failed:\n%s", infoLog);
                throw std::runtime_error(std::string("Shader compilation failed: ") + infoLog);
            }
        }

        ~Shader() {
            glDeleteShader(shader);
        }
    };
};
