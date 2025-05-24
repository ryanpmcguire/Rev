module;

#include <webgpu/wgpu.h>

export module Primitive;

import Topology;

export namespace WebGpu {

    struct Primitive {

        Topology topology;

        // Create
        Primitive(Topology topology) {
            this->topology = topology;
        }

        // Destroy
        ~Primitive() {
            
        }

        virtual void compute() {

        }

        virtual void sync(WGPUDevice& device) {

        }

        virtual void record(WGPURenderPassEncoder& pass) {

        }
    };
};