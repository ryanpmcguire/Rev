module;

#include <cstring>
#include <webgpu/wgpu.h>

export module Shader;

import Resource;

export namespace WebGpu {

    struct Shader {

        // For visibility
        enum class Stage {
            Vertex = WGPUShaderStage_Vertex,
            Fragment = WGPUShaderStage_Fragment
        };

        Resource file;
        WGPUShaderModule shader = nullptr;

        WGPUShaderModuleWGSLDescriptor wgslDesc;
        WGPUShaderModuleDescriptor shaderDesc;

        // Create
        Shader(WGPUDevice device, Resource file) {
            
            this->file = file;

            wgslDesc = {
                .chain = { .sType = WGPUSType_ShaderModuleWGSLDescriptor },
                .code = reinterpret_cast<const char*>(file.data)
            };

            shaderDesc = {
                .nextInChain = &wgslDesc.chain
            };

            shader = wgpuDeviceCreateShaderModule(device, &shaderDesc);
        }

        // Destroy
        ~Shader() {

            if (shader) { wgpuShaderModuleRelease(shader); }
        }

        void use(WGPURenderPassEncoder& pass) {

        }
    };
};