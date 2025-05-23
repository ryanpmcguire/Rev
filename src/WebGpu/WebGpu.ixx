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

import Primitive;

export namespace WebGpu {

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

    struct Surface {

        GLFWwindow* window = nullptr;

        WGPUSurfaceConfiguration config;
        WGPUSurface surface;

        // Managed
        Instance* instance = nullptr;
        Adapter* adapter = nullptr;
        inline static Device* device = nullptr;;
        RenderPass* renderPass = nullptr;

        WGPURenderPassColorAttachment colorAttachment = {
            //.view = targetView,
            .resolveTarget = nullptr,
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

            this->fit();
        }

        // Destroy
        ~Surface() {
            wgpuSurfaceUnconfigure(surface);
            wgpuSurfaceRelease(surface);
        }

        void fit() {

            // Get dimensions of framebuffer
            int width, height;
            glfwGetFramebufferSize(window, &width, &height);
            if (height == 0 || width == 0) { return; }

            // Set and reconfigure
            config.width = width; config.height = height;
            wgpuSurfaceUnconfigure(surface);
            wgpuSurfaceConfigure(surface, &config);
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
        void compute() {
            for (Primitive* primitive : primitives) {
                primitive->compute();
            }
        }

        // Sync all primitives
        void sync() {
            for (Primitive* primitive : primitives) {
                primitive->sync(device->device);
            }
        }

        // Record all primitives
        void record() {

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

            for (Primitive* primitive : primitives) {
                primitive->record(renderPass->renderPass);
            }

            renderPass->end();
        }

        void draw() {

            // Process flags
            if (flags.fit) { this->fit(); }
            if (flags.compute) { this->compute(); }
            if (flags.sync) { this->sync(); }

            // Get target view (what we're drawing to)
            WGPUTextureView targetView = this->getNextTextureView();
            if (!targetView) { return; }

            // Set color attachment view and record
            colorAttachment.view = targetView;
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