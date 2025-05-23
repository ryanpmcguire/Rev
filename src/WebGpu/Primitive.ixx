module;

#include <webgpu/wgpu.h>

export module Primitive;

export namespace WebGpu {

    struct Primitive {

        // Create
        Primitive() {

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