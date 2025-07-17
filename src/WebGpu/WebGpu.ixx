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
    struct ComputePass {

        Device* device = nullptr;

        WGPUComputePassDescriptor description;
        WGPUComputePassEncoder computePass;

        WGPUCommandEncoderDescriptor commandEncoderDesc = {};
        WGPUCommandEncoder encoder = nullptr;

        WGPUCommandBufferDescriptor commandBufferDesc = {};
        WGPUCommandBuffer commandBuffer = nullptr;

        // Create
        ComputePass(Device* device, WGPUComputePassDescriptor desc) {
            this->device = device;
            this->description = desc;
        }

        // Destroy
        ~ComputePass() {
            wgpuComputePassEncoderRelease(computePass);
            wgpuCommandEncoderRelease(encoder);
        }

        void begin() {
            encoder = wgpuDeviceCreateCommandEncoder(device->device, &commandEncoderDesc);
            computePass = wgpuCommandEncoderBeginComputePass(encoder, &description);
        }

        void end() {
            wgpuComputePassEncoderEnd(computePass);
            commandBuffer = wgpuCommandEncoderFinish(encoder, &commandBufferDesc);
        }

        void submit() {
            device->submit(commandBuffer);
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
        }

        void submit() {
            device->submit(commandBuffer);
        }
    };

    // RenderPass wrapper
    struct RenderBundle {

        Device* device = nullptr;

        WGPURenderBundleEncoderDescriptor desc = {};
        WGPURenderBundleEncoder encoder = nullptr;
        WGPURenderBundle bundle = nullptr;

        // Create
        RenderBundle(Device* device, WGPURenderBundleEncoderDescriptor desc) {
            this->device = device;
            this->desc = desc;
        }

        // Destroy
        ~RenderBundle() {
            if (bundle) { wgpuRenderBundleRelease(bundle); }
            if (encoder) { wgpuRenderBundleEncoderRelease(encoder); }
        }

        void begin() {
            encoder = wgpuDeviceCreateRenderBundleEncoder(device->device, &desc);
        }

        void end() {
            bundle = wgpuRenderBundleEncoderFinish(encoder, nullptr);
        }

        void execute(WGPURenderPassEncoder& pass) {
            wgpuRenderPassEncoderExecuteBundles(pass, 1, &bundle);
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
            .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc,
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
            textureDesc.sampleCount = sampleCount;
            viewDesc.format = format;
        }

        // Destroy
        ~TextureSurface() {

            // Destroy and release resources
            wgpuTextureDestroy(texture);
            wgpuTextureRelease(texture);
            wgpuTextureViewRelease(view);
        }

        // This allows us to resize only infrequently
        bool resizeIfNeeded(int width, int height) {

            // If we need to grow the image
            bool growWidth = width > this->width;
            bool growHeight = height > this->height;

            // If we need to shrink the image
            bool shrinkWidth = width < (this->width / 2);
            bool shrinkHeight = height < (this->height / 2);

            // If neither of these dimensions need to change, return
            if (!(growWidth || growHeight || shrinkWidth || shrinkHeight)) { return false; }
            
            int newWidth = width, newHeight = height;

            // Adjust width by a factor of 2
            if (growWidth) { newWidth *= 2; }
            else if (shrinkWidth) { newWidth /= 2; }

            // Adjust height by a factor of 2
            if (growHeight) { newHeight *= 2; }
            else if (shrinkHeight) { newHeight /= 2; }

            // One final guard for safety (should never be triggered)
            if (newWidth < width) { newWidth = width; }
            if (newHeight < height) { newHeight = height; }

            // Do resize
            resize(newWidth, newHeight);

            return true;
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
        TextureSurface* resolveTextureSurface = nullptr;
        
        ComputePass* computePass = nullptr;
        RenderPass* renderPass = nullptr;
        RenderBundle* renderBundle = nullptr;

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
        std::vector<Primitive*> dirtyPrimitives;

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
                .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopyDst,
                .viewFormatCount = 0,
                .alphaMode = WGPUCompositeAlphaMode_Auto,
                .presentMode = WGPUPresentMode_Fifo
            };

            msaaTextureSurface = new TextureSurface(device->device, config.format, 4);
            resolveTextureSurface = new TextureSurface(device->device, config.format, 1);

            this->fit();
        }

        // Destroy
        ~Surface() {

            delete adapter;
            delete instance;
            delete msaaTextureSurface;
            delete resolveTextureSurface;
            delete renderPass;

            wgpuSurfaceUnconfigure(surface);
            wgpuSurfaceRelease(surface);
        }

        struct TextureAndView { WGPUTexture texture; WGPUTextureView view; };
        TextureAndView getNextTextureView() {

            WGPUSurfaceTexture surfaceTexture;
            wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);
            
            if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success) {

                dbg("Failed to get texture");

                return { nullptr, nullptr };
            }

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

            return { surfaceTexture.texture, targetView };
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

            // Msaa color texture and view
            //--------------------------------------------------

            bool didResize = msaaTextureSurface->resizeIfNeeded(width, height);
            if (didResize) { resolveTextureSurface->resize(msaaTextureSurface->width, msaaTextureSurface->height); }

            flags.fit = false;

            if (didResize) {
                dbg("[WebGpu] Resized images");
                flags.record = true;
            }
        }

        // Compute all primitives
        void compute(uint32_t time) {

            dbg("[WebGpu] Computing");

            // Resize transform
            primitives[0]->transform->surfaceWidth = float(msaaTextureSurface->width);
            primitives[0]->transform->surfaceHeight = float(msaaTextureSurface->height);

            for (Primitive* primitive : dirtyPrimitives) {
                primitive->compute(time);
            }

            flags.compute = false;
        }

        // Sync all primitives
        void sync(uint32_t time) {

            dbg("[WebGpu] Syncing");

            for (Primitive* primitive : dirtyPrimitives) {
                primitive->sync(device->device, time);
            }

            flags.sync = false;
        }

        // Record all primitives
        void recordCompute() {

            dbg("[WebGpu] Recording compute");

            if (computePass) { delete computePass; }
            computePass = new ComputePass(device, {});

            // Record
            computePass->begin();

            // Bind globals
            primitives[0]->globalTimeBuffer->bind(computePass->computePass);
            
            for (Primitive* primitive : dirtyPrimitives) {
                primitive->record(computePass->computePass);
            }

            computePass->end();
        }

        void recordBundle() {

            dbg("[WebGpu] Recording bundle");

            if (renderBundle) { delete renderBundle; }

            renderBundle = new RenderBundle(device, {
                .nextInChain = nullptr,
                .colorFormatCount = 1,
                .colorFormats = &config.format,
                .depthStencilFormat = WGPUTextureFormat_Undefined,
                .sampleCount = msaaTextureSurface->sampleCount
            });

            renderBundle->begin();

            primitives[0]->globalTimeBuffer->bind(renderBundle->encoder);
            
            for (Primitive* primitive : primitives) {
                primitive->record(renderBundle->encoder);
            }

            renderBundle->end();

            flags.record = false;
        }

        void recordRender() {

            dbg("[WebGpu] Recording render");

            if (renderPass) { delete renderPass; }

            renderPass = new RenderPass(device, {
                .nextInChain = nullptr,
                .colorAttachmentCount = 1,
                .colorAttachments = &colorAttachment,
                .depthStencilAttachment = nullptr,
                .timestampWrites = nullptr,
            });

            renderPass->begin();
            renderBundle->execute(renderPass->renderPass);
            renderPass->end();
        }

        void draw(uint32_t time) {

            // Process flags
            if (flags.fit || false) { this->fit(); }
            if (flags.compute || true) { this->compute(time); }
            if (flags.sync || true) { this->sync(time); }

            // Sync global before draw
            primitives[0]->globalTimeBuffer->data.time = time;
            primitives[0]->globalTimeBuffer->dirty = true;
            primitives[0]->globalTimeBuffer->sync(device->device);

            // Set color attachment view and record
            colorAttachment.view = msaaTextureSurface->view;
            colorAttachment.resolveTarget = resolveTextureSurface->view;
            colorAttachment.clearValue = WGPUColor{ 0.9, 0.1, 0.2, 1.0 };

            if (flags.record || false) { this->recordBundle(); }
            recordCompute();
            recordRender();

            dbg("[WebGpu] Drawing");
            computePass->submit();
            renderPass->submit();
            
            // Resize but do not clear or realloc dirtyPrimitives
            dirtyPrimitives.resize(0);

            // Acquire swapchain view and texture
            TextureAndView swapchainTarget = getNextTextureView();
            if (!swapchainTarget.texture || !swapchainTarget.view) return;

            dbg("[WebGpu] Copying");

            // Command encoder to copy the texture
            WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device->device, nullptr);

            WGPUImageCopyTexture src = {
                .texture = resolveTextureSurface->texture,
                .mipLevel = 0,
                .origin = {0, 0, 0},
                .aspect = WGPUTextureAspect_All
            };

            WGPUImageCopyTexture dst = {
                .texture = swapchainTarget.texture,
                .mipLevel = 0,
                .origin = {0, 0, 0},
                .aspect = WGPUTextureAspect_All
            };

            WGPUExtent3D copySize = {
                .width = static_cast<uint32_t>(config.width),
                .height = static_cast<uint32_t>(config.height),
                .depthOrArrayLayers = 1
            };

            // Do the copy
            wgpuCommandEncoderCopyTextureToTexture(encoder, &src, &dst, &copySize);

            // Finish and submit
            WGPUCommandBuffer copyCommands = wgpuCommandEncoderFinish(encoder, nullptr);
            device->submit(copyCommands);

            // Cleanup
            wgpuCommandBufferRelease(copyCommands);
            wgpuCommandEncoderRelease(encoder);

            // Present surface, release texture view, poll device
            wgpuSurfacePresent(surface);
            wgpuTextureViewRelease(swapchainTarget.view);
            wgpuDevicePoll(device->device, false, nullptr);
        }
    };
}