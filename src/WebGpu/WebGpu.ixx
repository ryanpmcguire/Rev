module;

#include <GLFW/glfw3.h>
#include <glfw3webgpu/glfw3webgpu.h>
#include <webgpu/wgpu.h>
#include <webgpu/webgpu.h>

#define DEBUG true
#include <dbg.hpp>

#include "./webgpu-utils.h"

export module WebGpu;

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

        void submit(WGPUCommandBuffer* buffer) {
            wgpuQueueSubmit(queue, 1, buffer);
        }
    };

    // CommandEncoder wrapper
    struct CommandEncoder {

        WGPUCommandEncoderDescriptor description;
        WGPUCommandEncoder encoder;

        // Create
        CommandEncoder(WGPUDevice& device, WGPUCommandEncoderDescriptor desc) {
            this->description = desc;
            encoder = wgpuDeviceCreateCommandEncoder(device, &description);
        }

        // Destroy
        ~CommandEncoder() {
            wgpuCommandEncoderRelease(encoder);
        }
    };

    // RenderPass wrapper
    struct RenderPass {

        WGPURenderPassDescriptor description;
        WGPURenderPassEncoder renderPass;
        
        // Create
        RenderPass(WGPUCommandEncoder& encoder, WGPURenderPassDescriptor desc) {

            this->description = desc;
 
            renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &description);
            wgpuRenderPassEncoderEnd(renderPass);
        }

        // Destroy
        ~RenderPass() {
            wgpuRenderPassEncoderRelease(renderPass);
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
        Instance* instance;
        Adapter* adapter;
        Device* device;

        struct Flags {
            bool fit = false;
        };

        Flags flags;

        // Create
        Surface(GLFWwindow* window) {

            this->window = window;

            // Get instance
            Instance* instance = new Instance();

            // Get surface based on window
            surface = glfwGetWGPUSurface(instance->instance, window);

            // Create adapter
            adapter = new Adapter(instance->instance, {
                .nextInChain = nullptr, .compatibleSurface = surface
            });

            // Create device
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

        void draw() {

            if (flags.fit) { this->fit(); }

            WGPUTextureView targetView = this->getNextTextureView();
            if (!targetView) { return; }

            dbg("[WebGpu] Drawing");

            CommandEncoder* encoder = new CommandEncoder(device->device, {
                .nextInChain = nullptr,
                .label = "Command Encoder"
            });

            WGPURenderPassColorAttachment colorAttachment = {
                .view = targetView,
                .resolveTarget = nullptr,
                .loadOp = WGPULoadOp_Clear,
                .storeOp = WGPUStoreOp_Store,
                .clearValue = WGPUColor{ 0.9, 0.1, 0.2, 1.0 }
            };

            RenderPass* renderPass = new RenderPass(encoder->encoder, {
                .nextInChain = nullptr,
                .colorAttachmentCount = 1,
                .colorAttachments = &colorAttachment,
                .depthStencilAttachment = nullptr,
                .timestampWrites = nullptr,
            });

            CommandBuffer* commandBuffer = new CommandBuffer(encoder->encoder, {
                .nextInChain = nullptr,
                .label = "Command Buffer"
            });

            device->submit(&commandBuffer->commandBuffer);
            this->present();

            delete commandBuffer;
            delete renderPass;
            delete encoder;

            wgpuTextureViewRelease(targetView);
        }

        // Present surface
        void present() {
            wgpuSurfacePresent(surface);
        }
    };
}