module;

#include <webgpu/wgpu.h>

export module WebGpu.UniformBuffer;

import WebGpu.Shader;
import WebGpu.Buffer;

export namespace WebGpu {

    struct UniformBuffer : public Buffer {

        uint32_t index = 0;

        WGPUBindGroup bindGroup = nullptr;
        WGPUBindGroupLayout layout = nullptr;

        // Create
        struct Params { WGPUDevice device; void* data; size_t size; uint32_t group; size_t count = 1; };
        UniformBuffer(Params params) : Buffer(params.device, params.data, params.size, params.count) {
            
            this->index = params.group;

            desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            desc.size = params.size;

            // We can create our buffers now since the size is constant
            for (WGPUBuffer& buffer : buffers) {
                buffer = wgpuDeviceCreateBuffer(params.device, &desc);
            }

            // Layout entries
            //--------------------------------------------------

            // Create entries for each buffer
            std::vector<WGPUBindGroupLayoutEntry> layoutEntries(params.count);

            for (uint32_t i = 0; i < params.count; ++i) {

                layoutEntries[i] = {

                    .binding = i,
                    .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute,

                    .buffer = {
                        .type = WGPUBufferBindingType_Uniform,
                        .hasDynamicOffset = false,
                        .minBindingSize = size
                    }
                };
            }
    
            WGPUBindGroupLayoutDescriptor layoutDesc = {
                .entryCount = static_cast<uint32_t>(layoutEntries.size()),
                .entries = layoutEntries.data()
            };

            layout = wgpuDeviceCreateBindGroupLayout(params.device, &layoutDesc);

            // Bind-group entries
            //--------------------------------------------------

            std::vector<WGPUBindGroupEntry> groupEntries(params.count);
            for (uint32_t i = 0; i < params.count; ++i) {
                groupEntries[i] = {
                    .binding = i,
                    .buffer = buffers[i],
                    .offset = 0,
                    .size = size
                };
            }

            WGPUBindGroupDescriptor groupDesc = {
                .layout = layout,
                .entryCount = static_cast<uint32_t>(groupEntries.size()),
                .entries = groupEntries.data()
            };

            bindGroup = wgpuDeviceCreateBindGroup(device, &groupDesc);
        }

        // Destroy
        ~UniformBuffer() {
            if (bindGroup) wgpuBindGroupRelease(bindGroup);
            if (layout) wgpuBindGroupLayoutRelease(layout);
        }

        virtual void bind(WGPURenderPassEncoder& encoder) override {
            wgpuRenderPassEncoderSetBindGroup(encoder, index, bindGroup, 0, nullptr);
        }
    };
};