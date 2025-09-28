module;

#include <string>
#include <vector>
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
        Shader* shader = nullptr;

        // Create
        Pipeline(void* context) {

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

        void init(void* context, int floatsPerVertex = 0) {
            pipeline = metal_create_pipeline((MetalContext*)context, (MetalShader*)shader->shader, floatsPerVertex);
        }

        void bind(void* context) {
            metal_bind_pipeline((MetalContext*)context, pipeline);
        }
    };
};