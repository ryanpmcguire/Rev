module;

#include <vector>
#include <webgpu/wgpu.h>

export module VertexBuffer;

export namespace WebGpu {

    struct Vertex {
        float x, y;
    };

    struct VertexBuffer {

        WGPUBufferDescriptor desc = {
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .mappedAtCreation = false
        };

        std::vector<Vertex> data;
        bool dirty = true;

        // Create
        VertexBuffer() {

        }

        // Destroy
        ~VertexBuffer() {

        }

        // Sync with gpu
        void sync() {

            if (!dirty) { return; }

        }
    };
};