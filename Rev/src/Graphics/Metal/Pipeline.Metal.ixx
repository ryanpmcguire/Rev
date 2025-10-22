module;

#include <string>
#include <vector>
#include <stdexcept>
#include <dbg.hpp>

#include "./Helpers/MetalBackend.hpp"

export module Rev.Graphics.Pipeline;

import Rev.Core.Resource;
import Rev.Graphics.Shader;

export namespace Rev::Graphics {

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
        Pipeline(void* context, PipelineParams params, int floatsPerVertex = 0, bool instanced = true) {

            this->context = context;

            shader = new Shader(context, params.metalUniversal, Shader::Stage::Universal);
            pipeline = metal_create_pipeline((MetalContext*)context, (MetalShader*)shader->shader, floatsPerVertex, instanced);
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