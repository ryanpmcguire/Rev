module;

#include <webgpu/wgpu.h>

export module Triangles;

import WebGpu;
import Primitive;
import Shader;
import VertexBuffer;
import AttributeBuffer;
import Transform;
import Pipeline;
import Resources.Shaders.Triangles_wgsl;

export namespace WebGpu {

    struct Triangles : public Primitive {

        Surface* surface = nullptr;
        WGPUDevice device = nullptr;

        Transform* transform = nullptr;
        VertexBuffer* vertices = nullptr;
        AttributeBuffer* colors = nullptr;
        Shader* shader = nullptr;

        inline static Pipeline* pipeline = nullptr;

        Triangles(Surface* surface, Topology topology) : Primitive(topology) {

            this->surface = surface;
            this->device = surface->device->device;

            // Create resources
            //--------------------------------------------------


            transform = new Transform(device, 0);
            vertices = new VertexBuffer(device, 0);
            colors = new AttributeBuffer(device, 1);

            shader = new Shader(device, Triangles_wgsl);

            if (!pipeline) {
                pipeline = new Pipeline(surface, shader, topology, { vertices }, { colors });
            }

            surface->primitives.push_back(this);
        }

        ~Triangles() {

            delete shader;
            delete vertices;
            delete colors;
        }

        // Sync data
        void sync(WGPUDevice& device) override {

            vertices->sync(device);
            colors->sync(device);
        }

        // Record commands
        void record(WGPURenderPassEncoder& pass) override {

            pipeline->bind(pass);
            vertices->bind(pass);
            colors->bind(pass);


            wgpuRenderPassEncoderDraw(pass, uint32_t(vertices->members.size()), 1, 0, 0);
        }
    };
}