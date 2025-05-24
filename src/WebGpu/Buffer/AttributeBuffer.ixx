module;

#include <vector>
#include <webgpu/wgpu.h>

export module AttributeBuffer;

import Buffer;

export namespace WebGpu {

    struct AttributeBuffer : public Buffer {

        struct Color {
            float r, g, b, a;
        };

        WGPUVertexAttribute attrib = {
            .format = WGPUVertexFormat_Float32x4,
            .offset = 0,
            .shaderLocation = 0
        };

        WGPUVertexBufferLayout layout = {
            .arrayStride = sizeof(Color),
            .stepMode = WGPUVertexStepMode_Vertex,
            .attributeCount = 1,
            .attributes = &attrib
        };

        std::vector<Color> members;

        // Create
        AttributeBuffer(WGPUDevice device, uint32_t location = 0) : Buffer(device) {
            desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            attrib.shaderLocation = location;
        }

        // Destroy
        ~AttributeBuffer() {

        }

        // Set size before sync
        void sync(WGPUDevice& device) override {
            this->data = members.data();
            this->size = members.size() * sizeof(Color);
            Buffer::sync(device);
        }

        void bind(WGPURenderPassEncoder& encoder) override {
            wgpuRenderPassEncoderSetVertexBuffer(encoder, attrib.shaderLocation, buffer, 0, WGPU_WHOLE_SIZE);
        }
    };
};