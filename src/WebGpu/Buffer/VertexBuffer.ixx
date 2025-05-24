module;

#include <vector>
#include <webgpu/wgpu.h>

export module VertexBuffer;

import Buffer;

export namespace WebGpu {

    struct VertexBuffer : public Buffer {

        struct Vertex {
            float x, y;
        };

        WGPUVertexAttribute attrib = {
            .format = WGPUVertexFormat_Float32x2,
            .offset = 0,
            .shaderLocation = 0
        };

        WGPUVertexBufferLayout layout = {
            .arrayStride = sizeof(Vertex),
            .stepMode = WGPUVertexStepMode_Vertex,
            .attributeCount = 1,
            .attributes = &attrib
        };

        std::vector<Vertex> members;

        // Create
        VertexBuffer(WGPUDevice device, uint32_t location = 0) : Buffer(device) {
            desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            attrib.shaderLocation = location;
        }

        // Destroy
        ~VertexBuffer() {

        }

        // Set size before sync
        void sync(WGPUDevice& device) override {
            this->data = members.data();
            this->size = members.size() * sizeof(Vertex);
            Buffer::sync(device);
        }

        void bind(WGPURenderPassEncoder& encoder) override {
            wgpuRenderPassEncoderSetVertexBuffer(encoder, attrib.shaderLocation, buffer, 0, WGPU_WHOLE_SIZE);
        }
    };
};