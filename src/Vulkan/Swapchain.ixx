module;

#include <vector>
#include <stdexcept>

#include <dbg.hpp>
#include <volk/volk.h>

export module Vulkan.Swapchain;

export namespace Vulkan {

    struct Swapchain {

        VkDevice device;
        VkQueue presentQueue;

        // Semaphore
        VkSemaphoreCreateInfo semaphoreInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkSemaphore imageAvailableSemaphore;
        VkSemaphore renderFinishedSemaphore;

        // Fence
        VkFenceCreateInfo fenceInfo = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
        VkFence inFlightFence;

        // Info
        VkSwapchainCreateInfoKHR info;
        VkSwapchainKHR swapchain;

        // Images
        VkImageViewCreateInfo viewInfo;
        std::vector<VkImage> images;
        std::vector<VkImageView> views;

        // Create
        Swapchain(VkSurfaceKHR surface, VkDevice device, VkSurfaceFormatKHR format, VkQueue presentQueue, VkSwapchainCreateInfoKHR info) {

            dbg("[Vulkan][Swapchain] Creating swapchain");

            this->device = device;
            this->info = info;
            
            // Create sync objects
            //--------------------------------------------------

            VkResult iaErr = vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphore);
            VkResult rfErr = vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphore);
            VkResult cfErr = vkCreateFence(device, &fenceInfo, nullptr, &inFlightFence);

            if (iaErr || rfErr || cfErr) { throw std::runtime_error("Couldn't create sync objects!"); }

            // Create swapchain
            //--------------------------------------------------

            VkResult scResult = vkCreateSwapchainKHR(device, &info, nullptr, &swapchain);
            if (scResult != VK_SUCCESS) { throw std::runtime_error("Failed to create swapchain!"); }

            // Create image views
            //--------------------------------------------------

            // Prepare form
            VkImageViewCreateInfo viewInfo = {

                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = format.format,

                .components = {
                    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .a = VK_COMPONENT_SWIZZLE_IDENTITY
                },

                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            };

            size_t imageCount = this->getImages();
            views.resize(images.size());

            for (size_t i = 0; i < imageCount; i++) {

                viewInfo.image = images[i];

                VkResult err = vkCreateImageView(device, &viewInfo, nullptr, &(views[i]));
                if (err) { throw std::runtime_error("[Swapchain] Couldn't create image view!"); } 
            }
        }

        // Destroy
        ~Swapchain() {

            dbg("[Vulkan][Swapchain] Destroying swapchain");

            // Destroy sync objects
            vkDestroyFence(device, inFlightFence, nullptr);
            vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
            vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);

            // Destroy views and swapchain
            for (VkImageView& view : views) { vkDestroyImageView(device, view, nullptr); }
            vkDestroySwapchainKHR(device, swapchain, nullptr);
        }

        size_t getImages() {
        
            uint32_t imageCount = 0;
            vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
            images.resize(imageCount);
            vkGetSwapchainImagesKHR(device, swapchain, &imageCount, images.data());
        
            return imageCount;
        }

        uint32_t getNextImage() {
        
            // Draw with vulkan
            vkWaitForFences(device, 1, &(inFlightFence), VK_TRUE, UINT64_MAX);
            vkResetFences(device, 1, &(inFlightFence));
        
            uint32_t imageIndex;
            VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
            if (result != VK_SUCCESS) { throw std::runtime_error("Failed to acquire swapchain image!"); }
        
            return imageIndex;
        }
        
        void presentImage(uint32_t imageIndex) {
        
            VkSemaphore signalSemaphores[] = { renderFinishedSemaphore };
        
            // Present image
            VkPresentInfoKHR presentInfo = {
                .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = signalSemaphores,
                .swapchainCount = 1,
                .pSwapchains = &swapchain,
                .pImageIndices = &imageIndex
            };
        
            VkResult result = vkQueuePresentKHR(presentQueue, &presentInfo);
            if (result != VK_SUCCESS) { throw std::runtime_error("Failed to present swapchain image!"); }
        }
    };
}