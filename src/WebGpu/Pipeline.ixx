module;

#include <vector>
#include <webgpu/wgpu.h>

export module WebGpu.Pipeline;

import WebGpu;
import WebGpu.VertexBuffer;
import WebGpu.UniformBuffer;
import WebGpu.AnimatedBuffer;
import WebGpu.Shader;
import WebGpu.Primitive;
import WebGpu.Topology;

export namespace WebGpu {

    struct Pipeline {

        WGPUBlendState blendState = {
            .color = {
                .operation = WGPUBlendOperation_Add,
                .srcFactor = WGPUBlendFactor_SrcAlpha,
                .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
            },
            .alpha = {
                .operation = WGPUBlendOperation_Add,
                .srcFactor = WGPUBlendFactor_One,
                .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
            }
        };

        WGPUColorTargetState colorTarget = {
            //Set: .format = format,
            .blend = &blendState,
            .writeMask = WGPUColorWriteMask_All
        };

        WGPUVertexState vertex = {
            //Set: .module = shader->shader,
            .entryPoint = "vs_main",
            //.bufferCount = 1,
            //Set: .buffers = &layout
        };

        WGPUFragmentState fragment = {
            //Set: .module = shader->shader,
            .entryPoint = "fs_main",
            .targetCount = 1,
            .targets = &colorTarget
        };

        WGPUPrimitiveState primitive = {
            //.topology = WGPUPrimitiveTopology_TriangleList,
            .stripIndexFormat = WGPUIndexFormat_Undefined,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = WGPUCullMode_None
        };

        WGPUMultisampleState multisample = {
            .count = 4,
            .mask = ~0u,
            .alphaToCoverageEnabled = false
        };

        WGPURenderPipelineDescriptor desc = {
            .label = "Pipeline",
            //.vertex = vertex,
            //.primitive = primitive,
            .depthStencil = nullptr,
            .multisample = multisample,
            .fragment = &fragment,
        };

        WGPURenderPipeline renderPipeline;
        WGPUComputePipeline computePipeline;

        struct Params {

            struct Shaders {
                Shader* compute = nullptr;
                Shader* vertex = nullptr;
                Shader* fragment = nullptr;
            };

            struct Buffers {
                std::vector<VertexBuffer*> vertex;
                std::vector<UniformBuffer*> uniform;
                std::vector<AnimatedBuffer*> animated;
            };

            Surface* surface = nullptr;
            Topology topology;
            Shaders shaders;
            Buffers buffers;
        };

        // Create
        Pipeline(Params params) {

            // Bindable buffers
            //--------------------------------------------------

            std::vector<WGPUBindGroupLayout> renderBindGroupLayouts;
            std::vector<WGPUBindGroupLayout> computeBindGroupLayouts;

            for (UniformBuffer* ub : params.buffers.uniform) { renderBindGroupLayouts.push_back(ub->layout); }
            for (UniformBuffer* ub : params.buffers.uniform) { computeBindGroupLayouts.push_back(ub->layout); }
            for (AnimatedBuffer* ab : params.buffers.animated) { renderBindGroupLayouts.push_back(ab->renderLayout); }
            for (AnimatedBuffer* ab : params.buffers.animated) { computeBindGroupLayouts.push_back(ab->computeLayout); }

            WGPUPipelineLayoutDescriptor renderPipelineLayoutDesc = {
                .bindGroupLayoutCount = static_cast<uint32_t>(renderBindGroupLayouts.size()),
                .bindGroupLayouts = renderBindGroupLayouts.data()
            };

            WGPUPipelineLayoutDescriptor computePipelineLayoutDesc = {
                .bindGroupLayoutCount = static_cast<uint32_t>(computeBindGroupLayouts.size()),
                .bindGroupLayouts = computeBindGroupLayouts.data()
            };

            desc.layout = wgpuDeviceCreatePipelineLayout(params.surface->device->device, &renderPipelineLayoutDesc);

            // Compute-related
            //--------------------------------------------------

            WGPUComputePipelineDescriptor computeDesc = {
                .layout = wgpuDeviceCreatePipelineLayout(params.surface->device->device, &computePipelineLayoutDesc),
                .compute = {
                    .module = params.shaders.compute->shader,
                    .entryPoint = "cs_main"
                }
            };

            // Vertex-related
            //--------------------------------------------------

            std::vector<WGPUVertexBufferLayout> bufferLayouts;

            for (VertexBuffer* vb : params.buffers.vertex) { bufferLayouts.push_back(vb->layout); }

            vertex.module = params.shaders.vertex->shader;
            vertex.buffers = bufferLayouts.data();
            vertex.bufferCount = static_cast<uint32_t>(bufferLayouts.size());
            desc.vertex = vertex;

            // Fragment-related
            //--------------------------------------------------

            fragment.module = params.shaders.fragment->shader;
            colorTarget.format = params.surface->config.format;

            desc.primitive.topology = static_cast<WGPUPrimitiveTopology>(params.topology);

            // Create pipeline considering all prior
            computePipeline = wgpuDeviceCreateComputePipeline(params.surface->device->device, &computeDesc);
            renderPipeline = wgpuDeviceCreateRenderPipeline(params.surface->device->device, &desc);
        }

        // Destroy
        ~Pipeline() {
            wgpuRenderPipelineRelease(renderPipeline);
            wgpuComputePipelineRelease(computePipeline);
        }

        inline void bind(WGPURenderBundleEncoder& pass) {
            wgpuRenderBundleEncoderSetPipeline(pass, renderPipeline);
        }

        inline void bind(WGPURenderPassEncoder& pass) {
            wgpuRenderPassEncoderSetPipeline(pass, renderPipeline);
        }

        inline void bind(WGPUComputePassEncoder& pass) {
            wgpuComputePassEncoderSetPipeline(pass, computePipeline);
        }
    };
};