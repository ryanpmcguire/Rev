module;

#include <utility>
#include <webgpu/wgpu.h>

export module WebGpu.AnimatedBuffer;

import WebGpu.Buffer;

export namespace WebGpu {

    struct AnimatedBuffer {

        // WGPU handles
        WGPUDevice device = nullptr;
        WGPUQueue queue = nullptr;

        // Compute
        WGPUBindGroup computeBindGroup = nullptr;
        WGPUBindGroupLayout computeLayout = nullptr;

        // Render
        WGPUBindGroup renderBindGroup = nullptr;
        WGPUBindGroupLayout renderLayout = nullptr;

        // Buffers
        WGPUBuffer buff_a = nullptr;
        WGPUBuffer buff_b = nullptr;
        WGPUBuffer interp = nullptr;
        
        // Data, size, dirty
        uint32_t group = 0;
        void* data = nullptr;
        size_t size = 0;
        bool dirty = true;

        // Create
        struct Params { WGPUDevice device; void* data; size_t size; uint32_t group; };
        AnimatedBuffer(Params params) {

            device = params.device;
            queue = wgpuDeviceGetQueue(params.device);
            data = params.data;
            group = params.group;
            size = params.size;

            // Create buffers
            //--------------------------------------------------

            // Descriptor for uniform buffers (host-writable)
            WGPUBufferDescriptor uniformDesc = {
                .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
                .size = size,
                .mappedAtCreation = false
            };
            
            // Descriptor for storage buffers (interpolated)
            WGPUBufferDescriptor storageDesc = {
                .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
                .size = size,
                .mappedAtCreation = false
            };

            // Create two uniforms and one interpolated storage
            buff_b = wgpuDeviceCreateBuffer(device, &uniformDesc);
            buff_a = wgpuDeviceCreateBuffer(device, &uniformDesc);
            interp = wgpuDeviceCreateBuffer(device, &storageDesc);

            // Create layout
            //--------------------------------------------------

            WGPUBindGroupLayoutEntry computeEntries[4] = {
                {
                    .binding = 0, .visibility = WGPUShaderStage_Compute,
                    .buffer = { .type = WGPUBufferBindingType_Uniform, .hasDynamicOffset = false, .minBindingSize = size }
                },
                {
                    .binding = 1, .visibility = WGPUShaderStage_Compute,
                    .buffer = { .type = WGPUBufferBindingType_Uniform, .hasDynamicOffset = false, .minBindingSize = size }
                },
                {
                    .binding = 2, .visibility = WGPUShaderStage_Compute,
                    .buffer = { .type = WGPUBufferBindingType_Storage, .hasDynamicOffset = false, .minBindingSize = size }
                },
            };

            WGPUBindGroupLayoutEntry renderEntries[4] = {
                {
                    .binding = 0, .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
                    .buffer = { .type = WGPUBufferBindingType_Uniform, .hasDynamicOffset = false, .minBindingSize = size }
                },
                {
                    .binding = 1, .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
                    .buffer = { .type = WGPUBufferBindingType_Uniform, .hasDynamicOffset = false, .minBindingSize = size }
                },
                {
                    .binding = 2, .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
                    .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage, .hasDynamicOffset = false, .minBindingSize = size }
                },
            };

            WGPUBindGroupLayoutDescriptor computeLayoutDesc = {
                .entryCount = 3,
                .entries = computeEntries
            };

            WGPUBindGroupLayoutDescriptor renderLayoutDesc = {
                .entryCount = 3,
                .entries = renderEntries
            };

            computeLayout = wgpuDeviceCreateBindGroupLayout(device, &computeLayoutDesc);
            renderLayout = wgpuDeviceCreateBindGroupLayout(device, &renderLayoutDesc);

            // Create bind group
            //--------------------------------------------------

            WGPUBindGroupEntry groupEntries[4] = {
                { .binding = 0, .buffer = buff_a, .offset = 0, .size = size },
                { .binding = 1, .buffer = buff_b, .offset = 0, .size = size },
                { .binding = 2, .buffer = interp, .offset = 0, .size = size },
            };
            
            WGPUBindGroupDescriptor computeGroupDesc = { .layout = computeLayout, .entryCount = 3, .entries = groupEntries };
            WGPUBindGroupDescriptor renderGroupDesc = { .layout = renderLayout, .entryCount = 3, .entries = groupEntries };
            
            computeBindGroup = wgpuDeviceCreateBindGroup(device, &computeGroupDesc);
            renderBindGroup = wgpuDeviceCreateBindGroup(device, &renderGroupDesc);
        }

        // Destroy
        ~AnimatedBuffer() {

            if (computeBindGroup) { wgpuBindGroupRelease(computeBindGroup); }
            if (renderBindGroup) { wgpuBindGroupRelease(renderBindGroup); }

            if (buff_a) { wgpuBufferRelease(buff_a); }
            if (buff_b) { wgpuBufferRelease(buff_b); }
            if (interp) { wgpuBufferRelease(interp); }
        }

        void sync(WGPUDevice& device) {

            if (!dirty || !size) { return; }
            std::swap(buff_a, buff_b);

            wgpuQueueWriteBuffer(queue, buff_a, 0, data, size);
            dirty = false;
        }

        virtual void bind(WGPURenderPassEncoder& encoder) {
            wgpuRenderPassEncoderSetBindGroup(encoder, group, renderBindGroup, 0, nullptr);
        }

        virtual void bind(WGPUComputePassEncoder& encoder) {
            wgpuComputePassEncoderSetBindGroup(encoder, group, computeBindGroup, 0, nullptr);
        }
    };
};