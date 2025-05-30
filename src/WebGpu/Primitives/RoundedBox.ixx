module;

#include <webgpu/wgpu.h>

export module WebGpu.RoundedBox;

import WebGpu;
import WebGpu.Primitive;
import WebGpu.Shader;
import WebGpu.VertexBuffer;
import WebGpu.Transform;
import WebGpu.Pipeline;

import Resources.Shaders.RoundedBox.Compute_wgsl;
import Resources.Shaders.RoundedBox.Vertex_wgsl;
import Resources.Shaders.RoundedBox.Fragment_wgsl;

export namespace WebGpu {

    struct RoundedBox : public Primitive {

        struct Shared {
            
            struct Shaders {
                Shader* compute = nullptr;
                Shader* vertex = nullptr;
                Shader* fragment = nullptr;
            };

            Shaders shaders;
            Pipeline* pipeline = nullptr;
            bool initialized = false;
        };

        struct BoxData {
            float rect_x, rect_y, rect_w, rect_h;   // Rect
            float rad_tl, rad_tr, rad_bl, rad_br;   // Radii
            float fill_r, fill_g, fill_b, fill_a;   // Color
            uint32_t time, transition, a, b;              // Time of last update
        };

        inline static Shared shared;

        Surface* surface = nullptr;
        WGPUDevice device = nullptr;

        VertexBuffer* vertices = nullptr;

        BoxData boxData;
        AnimatedBuffer* boxDataBuffer;

        RoundedBox(Surface* surface) : Primitive(surface->device->device, Topology::TriangleList) {

            this->surface = surface;
            this->device = surface->device->device;

            // Create resources
            //--------------------------------------------------
            
            boxDataBuffer = new AnimatedBuffer({
                .device = device,
                .data = &boxData,
                .size = sizeof(BoxData),
                .group = 2
            });

            vertices = new VertexBuffer({ .device = device, .location = 0, .size = 6 });

            // Create shared objects
            if (!shared.initialized) {

                shared.shaders.compute = new Shader(device, Compute_wgsl);
                shared.shaders.vertex = new Shader(device, Vertex_wgsl);
                shared.shaders.fragment = new Shader(device, Fragment_wgsl);

                shared.pipeline = new Pipeline({

                    .surface = surface, 
                    .topology = topology,

                    .shaders = {
                        .compute = shared.shaders.compute,
                        .vertex = shared.shaders.vertex,
                        .fragment = shared.shaders.fragment
                    },

                    .buffers = {
                        .vertex = { vertices },
                        .uniform = { globalTimeBuffer },
                        .animated = { transform, boxDataBuffer }
                    },
                });
            }

            surface->primitives.push_back(this);
        }

        ~RoundedBox() {
            delete vertices;
        }

        // Sync data
        void sync(WGPUDevice& device, uint32_t time) override {

            transform->sync(device);
            boxDataBuffer->sync(device);
            vertices->sync(device);
        }

        void record(WGPUComputePassEncoder& pass) override {

            transform->bind(pass);
            boxDataBuffer->bind(pass);
            shared.pipeline->bind(pass);

            wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        }

        // Record commands
        void record(WGPURenderPassEncoder& pass) override {

            transform->bind(pass);
            boxDataBuffer->bind(pass);
            vertices->bind(pass);
            shared.pipeline->bind(pass);
            
            wgpuRenderPassEncoderDraw(pass, uint32_t(vertices->members.size()), 1, 0, 0);
        }

        // Record commands
        void record(WGPURenderBundleEncoder& pass) override {

            transform->bind(pass);
            boxDataBuffer->bind(pass);
            vertices->bind(pass);
            shared.pipeline->bind(pass);
            
            wgpuRenderBundleEncoderDraw(pass, uint32_t(vertices->members.size()), 1, 0, 0);
        }
    };
}