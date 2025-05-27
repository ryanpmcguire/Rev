module;

#include <vector>
#include <webgpu/wgpu.h>

export module WebGpu.VertexBuffer;

import WebGpu.Buffer;
import WebGpu.Vertex;

export namespace WebGpu {

    struct VertexBuffer : public Buffer {

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
        struct Params { WGPUDevice device; uint32_t location; size_t size = 0; size_t memberSize = sizeof(Vertex); };
        VertexBuffer(Params params) : Buffer(params.device) {

            desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            attrib.shaderLocation = params.location;

            // If there is a defined size, we can create our buffers immediately
            if (params.size) {

                members.resize(params.size);
                this->size = params.size;
                this->data = members.data();                

                for (WGPUBuffer& buffer : buffers) {
                    buffer = wgpuDeviceCreateBuffer(device, &desc);
                }
            }
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
            wgpuRenderPassEncoderSetVertexBuffer(encoder, attrib.shaderLocation, buffers[0], 0, WGPU_WHOLE_SIZE);
        }
    };
};