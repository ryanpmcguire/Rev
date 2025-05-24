module;

#include <webgpu/wgpu.h>

export module Transform;

import Shader;
import UniformBuffer;

export namespace WebGpu {

    struct Transform : public UniformBuffer {

        struct Matrix {
            float testVal = 0;
        };

        Matrix matrix;

        // Create
        Transform(WGPUDevice device, uint32_t index, Shader::Stage stage = Shader::Stage::Vertex)
        : UniformBuffer(device, &matrix, sizeof(Matrix), stage, index) {
            
        }

        // Destroy
        ~Transform() {

        }
    };
};