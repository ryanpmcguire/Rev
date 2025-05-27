module;

#include <webgpu/wgpu.h>

export module Triangles;

import WebGpu;
import WebGpu.Primitive;
import WebGpu.Shader;
import WebGpu.VertexBuffer;
import WebGpu.Transform;
import WebGpu.Pipeline;
import WebGpu.Color;
import Resources.Shaders.Triangles_wgsl;

export namespace WebGpu {

    struct Triangles : public Primitive {

        Surface* surface = nullptr;
        WGPUDevice device = nullptr;

        VertexBuffer* vertices = nullptr;
        VertexBuffer* colors = nullptr;
        
        inline static Shader* shader = nullptr;
        inline static Pipeline* pipeline = nullptr;

        Triangles(Surface* surface, Topology topology) : Primitive(surface->device->device, topology) {

            this->surface = surface;
            this->device = surface->device->device;

            // Create resources
            //--------------------------------------------------

            vertices = new VertexBuffer({ .device = device, .location = 0 });
            colors = new VertexBuffer({ .device = device, .location = 1, .memberSize = sizeof(Color) });

            if (!shader) { shader = new Shader(device, Triangles_wgsl); }
            if (!pipeline) { pipeline = new Pipeline(surface, shader, topology, { vertices, colors }, { globalTimeBuffer, transform }); }

            surface->primitives.push_back(this);
        }

        ~Triangles() {

            delete vertices;
            delete colors;
        }

        // Sync data
        void sync(WGPUDevice& device, uint32_t time) override {

            transform->sync(device);
            vertices->sync(device);
            colors->sync(device);
        }

        // Record commands
        void record(WGPURenderPassEncoder& pass) override {

            pipeline->bind(pass);
            transform->bind(pass);
            vertices->bind(pass);
            colors->bind(pass);

            wgpuRenderPassEncoderDraw(pass, uint32_t(vertices->members.size()), 1, 0, 0);
        }
    };
}