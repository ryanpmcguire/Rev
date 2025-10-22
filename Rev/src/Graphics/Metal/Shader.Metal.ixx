module;

#include <string>
#include <stdexcept>

#include "./Helpers/MetalBackend.hpp"

export module Rev.Graphics.Shader;

import Rev.Core.Resource;

export namespace Rev::Graphics {

    using namespace Rev::Core;

    struct Shader {

        enum Stage {
            Vertex,
            Fragment,
            Universal
        };

        //GLuint shader = 0;

        void* shader = nullptr;

        // Create
        Shader(void* context, Resource shaderFile, Stage shaderType) {

            const char* srcStr = reinterpret_cast<const char*>(shaderFile.data);

            shader = metal_create_shader(
                (MetalContext*)context,
                reinterpret_cast<const char*>(shaderFile.data),
                shaderFile.size
            );

            if (!shader) {
                throw std::runtime_error("Failed to create shader!");
            }
        }

        // Destroy
        ~Shader() {

            if (shader) {
                metal_destroy_shader(shader);
                shader = nullptr;
            }
        }
    };
};