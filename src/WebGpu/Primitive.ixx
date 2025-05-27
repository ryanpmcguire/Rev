module;

#include <webgpu/wgpu.h>

export module WebGpu.Primitive;

import WebGpu.Topology;
import WebGpu.GlobalTimeBuffer;
import WebGpu.Transform;

export namespace WebGpu {

    struct Primitive {

        WGPUDevice device = nullptr;
        Topology topology;

        inline static GlobalTimeBuffer* globalTimeBuffer = nullptr;
        Transform* transform = nullptr;
        Transform* computedTransform = nullptr;

        // Create
        Primitive(WGPUDevice device, Topology topology) {

            this->device = device;
            this->topology = topology;

            transform = new Transform(device, 0);
            if (!globalTimeBuffer) { globalTimeBuffer = new GlobalTimeBuffer(device, 1); }
        }

        // Destroy
        ~Primitive() {
            
        }

        virtual void compute(uint32_t time) {
            transform->compute(time);
        }

        virtual void sync(WGPUDevice& device, uint32_t time) {

        }

        virtual void record(WGPURenderPassEncoder& pass) {
        
        }
    };
};