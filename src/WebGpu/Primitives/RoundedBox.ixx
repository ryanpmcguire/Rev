module;

#include <webgpu/wgpu.h>

export module RoundedBox;

import WebGpu;
import Primitive;
import Shader;
import VertexBuffer;
import AttributeBuffer;
import Transform;
import Pipeline;
import Resources.Shaders.RoundedBox_wgsl;

export namespace WebGpu {

    struct RoundedBox : public Primitive {

        Surface* surface = nullptr;
        WGPUDevice device = nullptr;

        VertexBuffer* vertices = nullptr;
        
        inline static Shader* shader = nullptr;
        inline static Pipeline* pipeline = nullptr;

        struct BoxData {
            float rect_x, rect_y, rect_w, rect_h;   // Rect
            float rad_tl, rad_tr, rad_bl, rad_br;   // Radii
            float fill_r, fill_g, fill_b, fill_a;   // Color
        };

        BoxData boxData;
        UniformBuffer* boxDataBuffer;

        RoundedBox(Surface* surface) : Primitive(surface->device->device, Topology::TriangleList) {

            this->surface = surface;
            this->device = surface->device->device;

            // Create resources
            //--------------------------------------------------
            
            boxDataBuffer = new UniformBuffer(device, &boxData, sizeof(BoxData), 1, 1);

            vertices = new VertexBuffer(device, 0);

            if (!shader) { shader = new Shader(device, RoundedBox_wgsl); }
            if (!pipeline) { pipeline = new Pipeline(surface, shader, topology, { vertices }, { }, { transform, boxDataBuffer }); }

            surface->primitives.push_back(this);
        }

        ~RoundedBox() {
            delete vertices;
        }

        // Sync data
        void sync(WGPUDevice& device) override {

            transform->sync(device);
            boxDataBuffer->sync(device);
            vertices->sync(device);
        }

        // Record commands
        void record(WGPURenderPassEncoder& pass) override {

            pipeline->bind(pass);
            transform->bind(pass);
            boxDataBuffer->bind(pass);
            vertices->bind(pass);

            wgpuRenderPassEncoderDraw(pass, uint32_t(vertices->members.size()), 1, 0, 0);
        }
    };
}