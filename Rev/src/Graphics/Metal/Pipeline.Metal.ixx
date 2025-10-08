module;

#include <string>
#include <vector>
#include <stdexcept>
#include <dbg.hpp>

#include "./Helpers/MetalBackend.hpp"

export module Rev.Metal.Pipeline;

import Resource;
import Rev.Metal.Shader;

export namespace Rev {

    struct Pipeline {

        enum Topology {
            TriangleList,
            TriangleFan
        };

        void* context = nullptr;
        void* pipeline = nullptr;
        Shader* shader = nullptr;

        struct PipelineParams {

            Resource openGlVert;
            Resource openGlFrag;

            Resource metalUniversal;

            Resource vulkanVert;
            Resource vulkanFrag;
        };

        // Create
        Pipeline(void* context, PipelineParams params, int floatsPerVertex = 0) {

            this->context = context;

            shader = new Shader(context, params.metalUniversal, Shader::Stage::Universal);
            pipeline = metal_create_pipeline((MetalContext*)context, (MetalShader*)shader->shader, floatsPerVertex);
        }

        // Destroy
        ~Pipeline() {

            if (shader) {
                delete shader;
            }

            if (pipeline) {
                metal_destroy_pipeline(pipeline);
                pipeline = nullptr;
            }
        }

        void bind() {
            metal_bind_pipeline((MetalContext*)context, pipeline);
        }
    };
};