module;

#include <stdexcept>
#include <vector>
#include <dbg.hpp>
#include <volk/volk.h>

export module Vulkan.CommandPool;

export namespace Vulkan {

    struct CommandPool {

        VkDevice device;

        // Command pool
        VkCommandPoolCreateInfo info;
        VkCommandPool commandPool;

        // Command buffers
        VkCommandBufferAllocateInfo allocInfo;
        std::vector<VkCommandBuffer> commandBuffers;

        // Create
        CommandPool(VkDevice device, std::vector<VkFramebuffer>& framebuffers, int graphicsFamilyIndex) {

            dbg("[CommandPool] Creating CommandPool");

            // Create command pool
            //--------------------------------------------------

            this->device = device;

            info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                .queueFamilyIndex = static_cast<uint32_t>(graphicsFamilyIndex)
            };

            VkResult err = vkCreateCommandPool(device, &info, nullptr, &commandPool);
            if (err) { throw std::runtime_error("[CommandPool] Couldn't create CommandPool!"); }

            // Allocate command buffers
            //--------------------------------------------------

            commandBuffers.resize(framebuffers.size());

            allocInfo = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = commandPool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = static_cast<uint32_t>(commandBuffers.size())
            };

            VkResult allocErr = vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data());
            if (allocErr) { throw std::runtime_error("[CommandPool] Couldn't allocate command buffers!"); }
        }

        // Destroy
        ~CommandPool() {
            
            dbg("[CommandPool] Destroying CommandPool");

            vkDestroyCommandPool(device, commandPool, nullptr);
        }

        void recreate(std::vector<VkFramebuffer>& framebuffers) {

            // Destroy old command buffers
            if (!commandBuffers.empty()) {
                vkFreeCommandBuffers(device, commandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
            }

            // Resize
            commandBuffers.resize(framebuffers.size());
            allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

            // Create new
            VkResult err = vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data());
            if (err) { throw std::runtime_error("[CommandPool] Couldn't re-allocate command buffers!"); }
        }
    };
};