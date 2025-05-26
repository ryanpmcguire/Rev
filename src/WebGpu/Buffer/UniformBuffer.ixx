module;

#include <webgpu/wgpu.h>

export module UniformBuffer;

import Shader;
import Buffer;

export namespace WebGpu {

    struct UniformBuffer : public Buffer {

        uint32_t index = 0;

        WGPUBindGroup bindGroup = nullptr;
        WGPUBindGroupLayout layout = nullptr;

        // Create
        UniformBuffer(WGPUDevice device, void* data, size_t size, uint32_t index, size_t numBuffers = 1) : Buffer(device, data, size, numBuffers) {
            
            this->index = index;

            desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            desc.size = size;

            // We can create our buffers now since the size is constant
            for (WGPUBuffer& buffer : buffers) {
                buffer = wgpuDeviceCreateBuffer(device, &desc);
            }

            // Layout entries
            //--------------------------------------------------

            // Create entries for each buffer
            std::vector<WGPUBindGroupLayoutEntry> layoutEntries(numBuffers);

            for (uint32_t i = 0; i < numBuffers; ++i) {

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

            layout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

            // Bind-group entries
            //--------------------------------------------------

            std::vector<WGPUBindGroupEntry> groupEntries(numBuffers);
            for (uint32_t i = 0; i < numBuffers; ++i) {
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