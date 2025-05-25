module;

#include <webgpu/wgpu.h>

export module UniformBuffer;

import Shader;
import Buffer;

export namespace WebGpu {

    struct UniformBuffer : public Buffer {

        Shader::Stage stage;
        uint32_t index = 0;

        WGPUBindGroup bindGroup = nullptr;
        WGPUBindGroupLayout layout = nullptr;

        // Create
        UniformBuffer(WGPUDevice device, void* data, size_t size, Shader::Stage stage, uint32_t index) : Buffer(device, data, size) {
            
            this->stage = stage;
            this->index = index;

            // We can create our buffer now since the size will be constant
            desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            desc.size = size;
            buffer = wgpuDeviceCreateBuffer(device, &desc);

            // Create layout info
            //--------------------------------------------------

            WGPUBindGroupLayoutEntry layoutEntry = {
                .binding = 0,
                .visibility = static_cast<WGPUShaderStageFlags>(stage),
                .buffer = {
                    .type = WGPUBufferBindingType_Uniform,
                    .hasDynamicOffset = false,
                    .minBindingSize = size
                }
            };
    
            WGPUBindGroupLayoutDescriptor layoutDesc = {
                .entryCount = 1,
                .entries = &layoutEntry
            };

            layout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

            WGPUBindGroupEntry groupEntry = {
                .binding = 0,
                .buffer = buffer,
                .offset = 0,
                .size = size
            };

            WGPUBindGroupDescriptor groupDesc = {
                .layout = layout,
                .entryCount = 1,
                .entries = &groupEntry
            };

            bindGroup = wgpuDeviceCreateBindGroup(device, &groupDesc);
        }

        // Destroy
        ~UniformBuffer() {

        }

        virtual void bind(WGPURenderPassEncoder& encoder) override {
            wgpuRenderPassEncoderSetBindGroup(encoder, index, bindGroup, 0, nullptr);
        }
    };
};