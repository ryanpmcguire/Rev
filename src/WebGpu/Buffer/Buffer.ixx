module;

#include <vector>
#include <webgpu/wgpu.h>

export module WebGpu.Buffer;

export namespace WebGpu {

    // Generic buffer wrapper
    struct Buffer {
        
        WGPUDevice device = nullptr;
        WGPUQueue queue = nullptr;
        
        // We (may) have multiple buffers (usually one or two)
        std::vector<WGPUBuffer> buffers;
        size_t numBuffers = 1;
        size_t currentBuffer = 0;

        WGPUBufferDescriptor desc = {
            // .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .mappedAtCreation = false
        };

        // Data, size, dirty
        void* data = nullptr;
        size_t size = 0;
        bool dirty = true;

        // Create
        Buffer(WGPUDevice device, void* data = nullptr, size_t size = 0, size_t numBuffers = 1) 
            : device(device), data(data), size(size), numBuffers(numBuffers) {

            // Get queue, resize buffers
            this->queue = wgpuDeviceGetQueue(device);
            this->buffers.resize(numBuffers);
        }

        // Destroy
        ~Buffer() {

            // Destroy all buffers
            for (WGPUBuffer& buffer : buffers) {
                wgpuBufferDestroy(buffer);
                wgpuBufferRelease(buffer);
            }
        }

        // Sync with device
        virtual void sync(WGPUDevice& device) {

            // Get buffer, advance index
            WGPUBuffer& buffer = buffers[currentBuffer];
            currentBuffer = (currentBuffer + 1) % numBuffers;

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