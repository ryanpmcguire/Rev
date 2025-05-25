module;

#include <webgpu/wgpu.h>

export module Primitive;

import Topology;
import Transform;

export namespace WebGpu {

    struct Primitive {

        WGPUDevice device = nullptr;
        Topology topology;

        Transform* transform = nullptr;
        Transform* computedTransform = nullptr;

        // Create
        Primitive(WGPUDevice device, Topology topology) {

            this->device = device;
            this->topology = topology;

            transform = new Transform(device, 0);
        }

        // Destroy
        ~Primitive() {
            
        }

        virtual void compute() {
            transform->compute();
        }

        virtual void sync(WGPUDevice& device) {

        }

        virtual void record(WGPURenderPassEncoder& pass) {
        
        }
    };
};