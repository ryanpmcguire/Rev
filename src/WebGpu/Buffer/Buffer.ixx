module;

#include <webgpu/wgpu.h>

export module Buffer;

export namespace WebGpu {

    // Generic buffer wrapper
    struct Buffer {
        
        WGPUDevice device = nullptr;
        WGPUQueue queue = nullptr;
        WGPUBuffer buffer = nullptr;

        WGPUBufferDescriptor desc = {
            // .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .mappedAtCreation = false
        };

        // Data, size, dirty
        void* data = nullptr;
        size_t size = 0;
        bool dirty = true;

        // Create
        Buffer(WGPUDevice device, void* data = nullptr, size_t size = 0) {

            this->device = device;
            this->data = data;
            this->size = size;

            this->queue = wgpuDeviceGetQueue(device);
        }

        // Destroy
        ~Buffer() {

            if (buffer) {
                wgpuBufferDestroy(buffer);
                wgpuBufferRelease(buffer);
            }
        }

        // Sync with device
        virtual void sync(WGPUDevice& device) {

            // If not dirty or size didn't change
            if (!dirty || !size) { return; }

            // Create or update buffer if necessary
            if (size != desc.size) {
                
                if (buffer) {
                    wgpuBufferDestroy(buffer);
                    wgpuBufferRelease(buffer);
                }

                desc.size = size;
                buffer = wgpuDeviceCreateBuffer(device, &desc);
            }

            // Upload data
            wgpuQueueWriteBuffer(queue, buffer, 0, data, size);
            dirty = false;
        }

        // To be overridden
        virtual void bind(WGPURenderPassEncoder& encoder) {

        }
    };
};