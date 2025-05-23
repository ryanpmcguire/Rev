module;

#include <vector>
#include <webgpu/wgpu.h>

export module VertexBuffer;

export namespace WebGpu {

    struct VertexBuffer {

        struct Vertex {
            float x, y;
        };

        enum class Topology {
            PointList = WGPUPrimitiveTopology_PointList,
            LineList = WGPUPrimitiveTopology_LineList,
            LineStrip = WGPUPrimitiveTopology_LineStrip,
            TriangleList = WGPUPrimitiveTopology_TriangleList,
            TriangleStrip = WGPUPrimitiveTopology_TriangleStrip,
        };

        WGPUBufferDescriptor desc = {
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .mappedAtCreation = false
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

        WGPUDevice device = nullptr;
        WGPUBuffer buffer = nullptr;

        Topology topology;
        std::vector<Vertex> data;
        bool dirty = true;

        // Create
        VertexBuffer(WGPUDevice device, Topology topology) {
            this->topology = topology;
        }

        // Destroy
        ~VertexBuffer() {

            // Free buffer if needed
            if (buffer) {
                wgpuBufferDestroy(buffer);
                wgpuBufferRelease(buffer);
            }
        }

        // Sync with gpu
        void sync(WGPUDevice& device) {

            if (!dirty || data.empty()) { return; }
            size_t bufferSize = data.size() * sizeof(Vertex);

            // Recreate buffer if needed
            if (!buffer || desc.size != bufferSize) {

                if (buffer) {
                    wgpuBufferDestroy(buffer);
                    wgpuBufferRelease(buffer);
                }

                desc.size = bufferSize;
                buffer = wgpuDeviceCreateBuffer(device, &desc);
            }

            // Upload data
            wgpuQueueWriteBuffer(
                wgpuDeviceGetQueue(device),
                buffer,
                0,
                data.data(),
                bufferSize
            );

            dirty = false;
        }

        void bind(WGPURenderPassEncoder& encoder, uint32_t slot = 0) {
            if (!buffer) { return; }
            wgpuRenderPassEncoderSetVertexBuffer(encoder, slot, buffer, 0, WGPU_WHOLE_SIZE);
        }
    };
};