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

export namespace Vulkan {

    struct Surface {

        struct Support {
            VkSurfaceCapabilitiesKHR capabilities;
            std::vector<VkSurfaceFormatKHR> formats;
            std::vector<VkPresentModeKHR> presentModes;
        };

        GLFWwindow* window = nullptr;
        VkInstance instance = nullptr;
        VkSurfaceKHR surface = nullptr;

        Device* device = nullptr;
        Swapchain* swapchain = nullptr;
        RenderPass* renderPass = nullptr;
        Framebuffers* framebuffers = nullptr;

        // Details
        Support support;
        VkSurfaceFormatKHR format = { .format = VK_FORMAT_B8G8R8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
        VkExtent2D extent;

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

            renderPass = new RenderPass(device->device, format);

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

            framebuffers = new Framebuffers(device->device, swapchain->views, renderPass->renderPass, extent);
        }

        // Destroy
        ~Surface() {

            dbg("[Vulkan][Surface] Destroying surface");

            delete framebuffers;
            delete renderPass;
            delete swapchain;
            delete device;
            
            vkDestroySurfaceKHR(instance, surface, nullptr);
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