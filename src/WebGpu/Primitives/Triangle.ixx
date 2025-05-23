module;

#include <webgpu/wgpu.h>

export module Triangle;

import WebGpu;
import Primitive;
import Shader;
import VertexBuffer;
import Pipeline;
import Resources.Shaders.Triangle_wgsl;

export namespace WebGpu {

    struct Triangle : public Primitive {

        Surface* surface = nullptr;
        VertexBuffer* vertices;
        Shader* shader;

        inline static Pipeline* pipeline = nullptr;

        Triangle(Surface* surface) : Primitive() {

            this->surface = surface;

            // Create resources
            //--------------------------------------------------
            
            vertices = new VertexBuffer(surface->device->device, VertexBuffer::Topology::TriangleList);
            shader = new Shader(surface->device->device, Triangle_wgsl);

            if (!pipeline) {
                pipeline = new Pipeline(surface, shader, vertices);
            }

            surface->primitives.push_back(this);
        }

        ~Triangle() {

            delete shader;
            delete vertices;
        }

        // Compute data
        void compute() override {

            vertices->data = {
                { -0.5f, -0.5f },
                {  0.5f, -0.5f },
                {  0.0f,  0.5f }
            };

            vertices->dirty = true;
        }

        // Sync data
        void sync(WGPUDevice& device) override {

            vertices->sync(device);
        }

        // Record commands
        void record(WGPURenderPassEncoder& pass) override {

            pipeline->bind(pass);
            vertices->bind(pass);

            wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        }
    };
}