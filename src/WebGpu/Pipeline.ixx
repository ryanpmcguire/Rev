module;

#include <webgpu/wgpu.h>

export module Pipeline;

import WebGpu;
import VertexBuffer;
import Shader;

export namespace WebGpu {

    struct Pipeline {

        WGPUColorTargetState colorTarget = {
            //Set: .format = format,
            .writeMask = WGPUColorWriteMask_All
        };

        WGPUFragmentState fragment = {
            //Set: .module = shader->shader,
            .entryPoint = "fs_main",
            .targetCount = 1,
            .targets = &colorTarget
        };

        WGPUVertexState vertex = {
            //Set: .module = shader->shader,
            .entryPoint = "vs_main",
            //.bufferCount = 1,
            //Set: .buffers = &layout
        };

        WGPUPrimitiveState primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .stripIndexFormat = WGPUIndexFormat_Undefined,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = WGPUCullMode_None
        };

        WGPUMultisampleState multisample = {
            .count = 1,
            .mask = ~0u,
            .alphaToCoverageEnabled = false
        };

        WGPURenderPipelineDescriptor desc = {
            .label = "Pipeline",
            //.vertex = vertex,
            .primitive = primitive,
            .depthStencil = nullptr,
            .multisample = multisample,
            .fragment = &fragment,
        };

        WGPURenderPipeline pipeline;

        // Create
        Pipeline(Surface* surface, Shader* shader, VertexBuffer* vertexBuffer) {

            colorTarget.format = surface->config.format;
            vertex.module = shader->shader;
            fragment.module = shader->shader;
            vertex.buffers = &vertexBuffer->layout;
            vertex.bufferCount = 1;
            desc.vertex = vertex;

            pipeline = wgpuDeviceCreateRenderPipeline(surface->device->device, &desc);
        }

        // Destroy
        ~Pipeline() {
            wgpuRenderPipelineRelease(pipeline);
        }

        inline void bind(WGPURenderPassEncoder& pass) {
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        }
    };
};