module;

#include <vector>
#include <webgpu/wgpu.h>

export module Pipeline;

import WebGpu;
import VertexBuffer;
import AttributeBuffer;
import UniformBuffer;
import Shader;
import Primitive;
import Topology;

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

        WGPURenderPipeline pipeline;

        // Create
        Pipeline(Surface* surface, Shader* shader, Topology topology,
            std::vector<VertexBuffer*> vertexBuffers = {},
            std::vector<AttributeBuffer*> attribBuffers = {},
            std::vector<UniformBuffer*> uniformBuffers = {}
        ) {

            // Uniform buffers
            //--------------------------------------------------

            std::vector<WGPUBindGroupLayout> bindGroupLayouts;

            for (UniformBuffer* ub : uniformBuffers) {
                bindGroupLayouts.push_back(ub->layout);
            }

            WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {
                .bindGroupLayoutCount = static_cast<uint32_t>(bindGroupLayouts.size()),
                .bindGroupLayouts = bindGroupLayouts.data()
            };

            desc.layout = wgpuDeviceCreatePipelineLayout(surface->device->device, &pipelineLayoutDesc);

            // Vertex-related
            //--------------------------------------------------

            std::vector<WGPUVertexBufferLayout> bufferLayouts;

            for (VertexBuffer* vb : vertexBuffers) { bufferLayouts.push_back(vb->layout); }
            for (AttributeBuffer* ab : attribBuffers) { bufferLayouts.push_back(ab->layout); }

            vertex.module = shader->shader;
            vertex.buffers = bufferLayouts.data();
            vertex.bufferCount = static_cast<uint32_t>(bufferLayouts.size());
            desc.vertex = vertex;

            // Fragment-related
            //--------------------------------------------------

            fragment.module = shader->shader;
            colorTarget.format = surface->config.format;

            desc.primitive.topology = static_cast<WGPUPrimitiveTopology>(topology);

            // Create pipeline considering all prior
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