module;

#include <vector>
#include <stdexcept>

#include <dbg.hpp>
#include <volk/volk.h>
#include <GLFW/glfw3.h>

export module Vulkan.Surface;

import Vulkan.Device;
import Vulkan.Swapchain;
import Vulkan.RenderPass;
import Vulkan.Framebuffers;
import Vulkan.CommandPool;

export namespace Vulkan {

    struct Surface {

        struct Support {
            VkSurfaceCapabilitiesKHR capabilities;
            std::vector<VkSurfaceFormatKHR> formats;
            std::vector<VkPresentModeKHR> presentModes;
        };

        struct Flags {
            bool resize = false;
            bool record = true;
        };

        GLFWwindow* window = nullptr;
        VkInstance instance = nullptr;
        VkSurfaceKHR surface = nullptr;

        Device* device = nullptr;
        Swapchain* swapchain = nullptr;
        RenderPass* renderPass = nullptr;
        Framebuffers* framebuffers = nullptr;
        CommandPool* commandPool = nullptr;

        // Details
        Support support;
        VkSurfaceFormatKHR format = { .format = VK_FORMAT_B8G8R8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
        VkExtent2D extent;

        Flags flags;

        // Create
        Surface(VkInstance instance, GLFWwindow* window) {

            dbg("[Vulkan][Surface] Creating surface");

            this->instance = instance;
            this->window = window;

            // Create surface
            VkResult surfaceErr = glfwCreateWindowSurface(instance, window, nullptr, &surface);
            if (surfaceErr) { throw std::runtime_error("[Vulkan][Surface] Failed to create surface"); }

            // Create device
            device = new Device(instance, surface);

            // Get support, choose format, etc...
            support = getSupport();
            format = getFormat();
            presentMode = getPresentMode();
            extent = getExtent();

            // Create swapchain
            swapchain = new Swapchain(surface, device->device, format, device->presentQueue, {

                .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                .surface = surface,
                .minImageCount = support.capabilities.minImageCount,
                .imageFormat = format.format,
                .imageColorSpace = format.colorSpace,
                .imageExtent = extent,
                .imageArrayLayers = 1,
                .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .preTransform = support.capabilities.currentTransform,
                .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                .presentMode = presentMode,
                .clipped = VK_TRUE,
                .oldSwapchain = VK_NULL_HANDLE
            });

            renderPass = new RenderPass(device->device, format);
            framebuffers = new Framebuffers(device->device, swapchain->views, renderPass->renderPass, extent);
            commandPool = new CommandPool(device->device, framebuffers->framebuffers, device->indices.graphicsFamily);
        }

        // Destroy
        ~Surface() {

            dbg("[Vulkan][Surface] Destroying surface");

            device->waitIdle();

            delete commandPool;
            delete framebuffers;
            delete renderPass;
            delete swapchain;
            delete device;
            
            vkDestroySurfaceKHR(instance, surface, nullptr);
        }

        void resize() {

            dbg("[Vulkan][Surface] Resizing...");

            device->waitIdle();

            extent = getExtent();

            swapchain->recreate(extent);
            renderPass->recreate();
            framebuffers->recreate(swapchain->views, renderPass->renderPass, extent);
            commandPool->recreate(framebuffers->framebuffers);

            flags.record = true;
        }

        void record() {

            dbg("[Vulkan][Surface] Recording...");

            for (size_t i = 0; i < commandPool->commandBuffers.size(); i++) {

                VkCommandBufferBeginInfo beginInfo{};
                beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            
                if (vkBeginCommandBuffer(commandPool->commandBuffers[i], &beginInfo) != VK_SUCCESS) {
                    throw std::runtime_error("Failed to begin recording command buffer!");
                }
        
                // Begin
                VkClearValue clearColor = { {{1.0f, 0.0f, 0.0f, 1.0f}} };

                VkRenderPassBeginInfo renderPassInfo = {
                    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                    .renderPass = renderPass->renderPass,
                    .framebuffer = framebuffers->framebuffers[i],
                    .renderArea = { .offset = {0, 0}, .extent = extent },
                    .clearValueCount = 1,
                    .pClearValues = &clearColor
                };
            
                vkCmdBeginRenderPass(commandPool->commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        
                /*for (Primitive* primitive : primitives) {
                    primitive->record(commandPool->commandBuffers[i]);
                }*/
        
                vkCmdEndRenderPass(commandPool->commandBuffers[i]);
            
                if (vkEndCommandBuffer(commandPool->commandBuffers[i]) != VK_SUCCESS) {
                    throw std::runtime_error("Failed to record command buffer!");
                }
            }
        }

        void draw() {

            // Call functions as needed based on flags
            if (flags.resize) { resize(); flags.resize = false; }
            if (flags.record) { record(); flags.record = false; }

            dbg("[Vulkan][Surface] Drawing...");

            uint32_t imageIndex = swapchain->getNextImage();

            // Submit rendering
            VkSubmitInfo submitInfo{};
            VkSemaphore waitSemaphores[] = { swapchain->imageAvailableSemaphore };
            VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
            VkSemaphore signalSemaphores[] = { swapchain->renderFinishedSemaphore };
            
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = waitSemaphores;
            submitInfo.pWaitDstStageMask = waitStages;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &(commandPool->commandBuffers[imageIndex]);
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = signalSemaphores;

            if (vkQueueSubmit(device->graphicsQueue, 1, &submitInfo, swapchain->inFlightFence) != VK_SUCCESS) {
                throw std::runtime_error("Failed to submit draw command buffer!");
            }

            swapchain->presentImage(imageIndex);
        }

        Support getSupport() {

            // Init
            uint32_t formatCount = 0;
            uint32_t presentModeCount = 0;
            VkPhysicalDevice physicalDevice = device->physicalDevice;
        
            // Query capabilities, formats, etc...
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &(support.capabilities));
            vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
            vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
        
            // We need these or else we can't proceed
            if (!formatCount || !presentModeCount) { throw std::runtime_error("Couldn't get formats and present modes!"); }
            support.formats.resize(formatCount);
            support.presentModes.resize(presentModeCount);
            
            // Query again, this time with data pointers
            vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, support.formats.data());
            vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, support.presentModes.data());
        
            return support;
        }

        // Try to use current format, change if needed
        VkSurfaceFormatKHR getFormat() {

            // Find matching
            for (VkSurfaceFormatKHR& candidate : support.formats) {

                if (candidate.format != format.format) { continue; }
                if (candidate.colorSpace != format.colorSpace) { continue; }

                return (format = candidate);
            }

            // Fallback: first available
            return (format = support.formats[0]);
        }
        
        // Try to use current present mode, change if needed
        VkPresentModeKHR getPresentMode() {

            // Find matching
            for (VkPresentModeKHR& candidate : support.presentModes) {
                if (candidate != presentMode) { continue; }
                return (presentMode = candidate);
            }

            // Fallback: first available
            return (presentMode = support.presentModes[0]);
        }

        // For now, just get currentExtent. Don't bother asking glfw
        VkExtent2D getExtent() {

            extent = support.capabilities.currentExtent;

            return extent;
        }
    };
}