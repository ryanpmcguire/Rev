module;

#include <webgpu/wgpu.h>

export module GlobalTimeBuffer;

import UniformBuffer;

export namespace WebGpu {

    struct GlobalTimeBuffer : public UniformBuffer {

        struct Data {
            uint32_t time = 0, t2, t3, t4;
        };

        Data data;
        
        // Create
        GlobalTimeBuffer(WGPUDevice device, uint32_t index)
        : UniformBuffer({ device, &data, sizeof(Data), index }) {

        }

        // Destroy
        ~GlobalTimeBuffer() {

        }
    };
};