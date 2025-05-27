module;

#include <vector>

#include <GLFW/glfw3.h>
#include <glfw3webgpu/glfw3webgpu.h>
#include <webgpu/wgpu.h>
#include <webgpu/webgpu.h>

#define DEBUG true
#include <dbg.hpp>

#include "./webgpu-utils.h"

export module WebGpu;

import Rev.GlobalTime;
import WebGpu.Primitive;

export namespace WebGpu {
    
    struct Instance {

        WGPUInstance instance;

        // Singleton
        static Instance* Ptr() {
            static Instance singleton;
            return &singleton;
        }

        // Create
        Instance() {
            instance = wgpuCreateInstance(nullptr);
        }

        // Destroy
        ~Instance() {
            wgpuInstanceRelease(instance);
        }
    };

    // Adapter wrapper
    struct Adapter {

        WGPURequestAdapterOptions options;
        WGPUAdapter adapter;

        // Create
        Adapter(WGPUInstance& instance, WGPURequestAdapterOptions opts) {
            this->options = opts;
            adapter = requestAdapterSync(instance, &options);
        }

        // Destroy
        ~Adapter() {
            wgpuAdapterRelease(adapter);
        }
    };

    struct Device {

        WGPUDeviceDescriptor descriptor;
        WGPUDevice device;
        WGPUQueue queue;

        // Create
        Device(WGPUAdapter& adapter, WGPUDeviceDescriptor desc) {
            this->descriptor = desc;
            device = requestDeviceSync(adapter, &descriptor);
            queue = wgpuDeviceGetQueue(device);
        }

        // Destroy
        ~Device() {
            wgpuQueueRelease(queue);
            wgpuDeviceRelease(device);
        }

        void submit(WGPUCommandBuffer buffer) {
            wgpuQueueSubmit(queue, 1, &buffer);
        }
    };

    // RenderPass wrapper
    struct RenderPass {

        Device* device = nullptr;

        WGPURenderPassDescriptor description;
        WGPURenderPassEncoder renderPass;

        WGPUCommandEncoderDescriptor commandEncoderDesc = {};
        WGPUCommandEncoder encoder = nullptr;

        WGPUCommandBufferDescriptor commandBufferDesc = {};
        WGPUCommandBuffer commandBuffer = nullptr;

        // Create
        RenderPass(Device* device, WGPURenderPassDescriptor desc) {
            this->device = device;
            this->description = desc;
        }

        // Destroy
        ~RenderPass() {
            wgpuRenderPassEncoderRelease(renderPass);
            wgpuCommandEncoderRelease(encoder);
        }

        void begin() {
            encoder = wgpuDeviceCreateCommandEncoder(device->device, &commandEncoderDesc);
            renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &description);
        }

        void end() {
            wgpuRenderPassEncoderEnd(renderPass);
            commandBuffer = wgpuCommandEncoderFinish(encoder, &commandBufferDesc);
            device->submit(commandBuffer);
        }
    };

    struct CommandBuffer {

        WGPUCommandBufferDescriptor description;
        WGPUCommandBuffer commandBuffer;
        
        // Create
        CommandBuffer(WGPUCommandEncoder& encoder, WGPUCommandBufferDescriptor desc) {
            this->description = desc;
            commandBuffer = wgpuCommandEncoderFinish(encoder, &description);
        }
        
        // Destroy
        ~CommandBuffer() {
            wgpuCommandBufferRelease(commandBuffer);
        }
    };

    // Texture + view manager
    struct TextureSurface {
        
        int width, height;
        uint32_t sampleCount = 4;

        // Managed
        WGPUDevice device = nullptr;
        WGPUTexture texture = nullptr;
        WGPUTextureView view = nullptr;
        
        // Texture descriptor
        WGPUTextureDescriptor textureDesc = {
            .nextInChain = nullptr,
            .usage = WGPUTextureUsage_RenderAttachment,
            .dimension = WGPUTextureDimension_2D,
            //.size = { uint32_t(width), uint32_t(height), 1 },
            //.format = config.format,
            .mipLevelCount = 1,
            .sampleCount = sampleCount,
        };

        // View descriptor
        WGPUTextureViewDescriptor viewDesc = {
            .nextInChain = nullptr,
            //.format = config.format,
            .dimension = WGPUTextureViewDimension_2D,
            .baseMipLevel = 0,
            .mipLevelCount = 1,
            .baseArrayLayer = 0,
            .arrayLayerCount = 1,
            .aspect = WGPUTextureAspect_All
        };

        // Create
        TextureSurface(WGPUDevice device, WGPUTextureFormat format, int sampleCount) {

            this->sampleCount = sampleCount;
            this->device = device;

            // Set initial configuration (format, samples, etc...)
            textureDesc.format = format;
            viewDesc.format = format;
        }

        // Destroy
        ~TextureSurface() {

            // Destroy and release resources
            wgpuTextureDestroy(texture);
            wgpuTextureRelease(texture);
            wgpuTextureViewRelease(view);
        }

        void resize(int width, int height) {

            this->width = width; this->height = height;

            textureDesc.size = { uint32_t(width), uint32_t(height), 1 };

            if (texture) {
                wgpuTextureDestroy(texture);
                wgpuTextureRelease(texture);
                wgpuTextureViewRelease(view);
            }

            texture = wgpuDeviceCreateTexture(device, &textureDesc);
            view = wgpuTextureCreateView(texture, &viewDesc);
        }
    };

    struct Surface {

        GLFWwindow* window = nullptr;

        WGPUSurfaceConfiguration config;
        WGPUSurface surface;

        // Managed
        Instance* instance = nullptr;
        Adapter* adapter = nullptr;
        inline static Device* device = nullptr;
        TextureSurface* msaaTextureSurface = nullptr;
        RenderPass* renderPass = nullptr;

        WGPURenderPassColorAttachment colorAttachment = {
            //.view = targetView,
            //.resolveTarget = nullptr,
            .loadOp = WGPULoadOp_Clear,
            .storeOp = WGPUStoreOp_Store,
            //.clearValue = WGPUColor{ 0.9, 0.1, 0.2, 1.0 }
        };

        struct Flags {
            bool fit = false;
            bool compute = true;
            bool sync = true;
            bool record = true;
        };

        Flags flags;

        std::vector<Primitive*> primitives;

        // Create
        Surface(GLFWwindow* window) {

            this->window = window;

            // Get instance
            Instance* instance = Instance::Ptr();

            // Get surface based on window
            surface = glfwGetWGPUSurface(instance->instance, window);

            // Create adapter
            adapter = new Adapter(instance->instance, {
                .nextInChain = nullptr, .compatibleSurface = surface
            });

            // Create device if needed
            if (!device) {
                
                device = new Device(adapter->adapter, {
                
                    .nextInChain = nullptr,
                    .label = "My Device",
                    .requiredFeatureCount = 0,
                    .requiredLimits = nullptr,

                    .defaultQueue = {
                        .nextInChain = nullptr,
                        .label = "Default Queue"
                    }
                });
            }

            config = {
                .nextInChain = nullptr,
                .device = device->device,
                .format = wgpuSurfaceGetPreferredFormat(surface, adapter->adapter),
                .usage = WGPUTextureUsage_RenderAttachment,
                .viewFormatCount = 0,
                .alphaMode = WGPUCompositeAlphaMode_Auto,
                .presentMode = WGPUPresentMode_Fifo
            };

            msaaTextureSurface = new TextureSurface(device->device, config.format, 4);

            this->fit();
        }

        // Destroy
        ~Surface() {

            delete adapter;
            delete instance;
            delete msaaTextureSurface;
            delete renderPass;

            wgpuSurfaceUnconfigure(surface);
            wgpuSurfaceRelease(surface);
        }

        void fit() {

            dbg("[WebGpu] Fitting");

            // Get dimensions of framebuffer
            int width, height;
            glfwGetFramebufferSize(window, &width, &height);
            if (height == 0 || width == 0) { return; }

            // Set and reconfigure
            config.width = width; config.height = height;
            wgpuSurfaceUnconfigure(surface);
            wgpuSurfaceConfigure(surface, &config);

            // Msaa color texture and view
            //--------------------------------------------------

            msaaTextureSurface->resize(width, height);

            flags.fit = false;

            // BAD: WE FORCE COMPUTE AND SYNC TO ALSO RUN
            flags.compute = true;
            flags.sync = true;
            flags.record = true;
        }

        WGPUTextureView getNextTextureView() {

            WGPUSurfaceTexture surfaceTexture;
            wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);
            if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success) { return nullptr; }

            // Create view
            WGPUTextureViewDescriptor viewDescriptor = {
                .nextInChain = nullptr,
                .label = "Surface Texture View",
                .format = wgpuTextureGetFormat(surfaceTexture.texture),
                .dimension = WGPUTextureViewDimension_2D,
                .baseMipLevel = 0,
                .mipLevelCount = 1,
                .baseArrayLayer = 0,
                .arrayLayerCount = 1,
                .aspect = WGPUTextureAspect_All
            };

            WGPUTextureView targetView = wgpuTextureCreateView(surfaceTexture.texture, &viewDescriptor);

            return targetView;
        }

        // Compute all primitives
        void compute(uint32_t time) {

            dbg("[WebGpu] Computing");

            // Resize transform
            primitives[0]->transform->surfaceWidth = float(config.width);
            primitives[0]->transform->surfaceHeight = float(config.height);

            for (Primitive* primitive : primitives) {
                primitive->compute(time);
            }

            flags.compute = false;
        }

        // Sync all primitives
        void sync(uint32_t time) {

            dbg("[WebGpu] Syncing");

            for (Primitive* primitive : primitives) {
                primitive->sync(device->device, time);
            }

            flags.sync = false;
        }

        // Record all primitives
        void record() {

            dbg("[WebGpu] Recording");

            if (renderPass) { delete renderPass; }

            renderPass = new RenderPass(device, {
                .nextInChain = nullptr,
                .colorAttachmentCount = 1,
                .colorAttachments = &colorAttachment,
                .depthStencilAttachment = nullptr,
                .timestampWrites = nullptr,
            });

            // Record
            renderPass->begin();

            // Bind globals
            primitives[0]->globalTimeBuffer->bind(renderPass->renderPass);
            
            for (Primitive* primitive : primitives) {
                primitive->record(renderPass->renderPass);
            }

            renderPass->end();

            flags.record = false;
        }

        void draw(uint32_t time) {

            // Process flags
            if (flags.fit) { this->fit(); }
            if (flags.compute) { this->compute(time); }
            if (flags.sync) { this->sync(time); }

            //wgpuSurfaceUnconfigure(surface);
            wgpuSurfaceConfigure(surface, &config);

            // Get target view (what we're drawing to)
            WGPUTextureView targetView = this->getNextTextureView();
            if (!targetView) { return; }

            // Sync global before draw
            primitives[0]->globalTimeBuffer->data.time = time;
            primitives[0]->globalTimeBuffer->dirty = true;
            primitives[0]->globalTimeBuffer->sync(device->device);

            // Set color attachment view and record
            colorAttachment.view = msaaTextureSurface->view;
            colorAttachment.resolveTarget = targetView;
            colorAttachment.clearValue = WGPUColor{ 0.9, 0.1, 0.2, 1.0 };
            if (flags.record) { this->record(); }

            dbg("[WebGpu] Drawing");

            // Present surface, release texture view, poll device
            wgpuSurfacePresent(surface);
            wgpuTextureViewRelease(targetView);
            wgpuDevicePoll(device->device, false, nullptr);
        }
    };
}