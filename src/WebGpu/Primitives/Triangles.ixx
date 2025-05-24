module;

#include <webgpu/wgpu.h>

export module Triangles;

import WebGpu;
import Primitive;
import Shader;
import VertexBuffer;
import AttributeBuffer;
import Pipeline;
import Resources.Shaders.Triangles_wgsl;

export namespace WebGpu {

    struct Triangles : public Primitive {

        Surface* surface = nullptr;
        VertexBuffer* vertices;
        AttributeBuffer* colors;
        Shader* shader;

        inline static Pipeline* pipeline = nullptr;

        Triangles(Surface* surface, Topology topology) : Primitive(topology) {

            this->surface = surface;

            // Create resources
            //--------------------------------------------------
            
            vertices = new VertexBuffer(surface->device->device, 0);
            colors = new AttributeBuffer(surface->device->device, 1);

            shader = new Shader(surface->device->device, Triangles_wgsl);

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

        // Compute data
        void compute() override {

            vertices->dirty = true;
            vertices->members = {
                { -0.5f, -0.5f },
                {  0.5f, -0.5f },
                {  0.0f,  0.5f }
            };

            colors->dirty = true;
            colors->members = {
                { 1, 0, 0, 1 },
                { 0, 1, 0, 1 },
                { 0, 0, 1, 1 }
            };
        }

        // Sync data
        void sync(WGPUDevice& device) override {

            vertices->sync(device);
            colors->sync(device);
        }

        // Record commands
        void record(WGPURenderPassEncoder& pass) override {

            pipeline->bind(pass);
            vertices->bind(pass, 0);
            colors->bind(pass, 1);

            wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        }
    };
}