module;

#include <stdexcept>
#include <vector>
#include <dbg.hpp>
#include <volk/volk.h>

export module Vulkan.Framebuffers;

export namespace Vulkan {

    struct Framebuffers {

        VkDevice device = nullptr;

        VkFramebufferCreateInfo info;
        std::vector<VkFramebuffer> framebuffers;

        // Create
        Framebuffers(VkDevice device, std::vector<VkImageView>& views, VkRenderPass renderPass, VkExtent2D extent) {

            this->device = device;

            info = {

                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = renderPass,
                .attachmentCount = 1,
                .width = extent.width,
                .height = extent.height,
                .layers = 1,
            };
        
            size_t numViews = views.size();
            framebuffers.resize(numViews);
        
            for (size_t i = 0; i < numViews; i++) {
        
                VkImageView attachments[] = { views[i] };
                info.pAttachments = attachments;
        
                VkResult err = vkCreateFramebuffer(device, &info, nullptr, &(framebuffers[i]));
                if (err) { throw std::runtime_error("[Framebuffers] Couldn't create framebuffer!"); }
            }
        }

        // Destroy
        ~Framebuffers() {

            dbg("[Framebuffers] Destroying Framebuffers");

            for (VkFramebuffer framebuffer : framebuffers) {
                vkDestroyFramebuffer(device, framebuffer, nullptr);
            }
        }

        void recreate(std::vector<VkImageView>& views, VkRenderPass renderPass, VkExtent2D extent) {

            // Destroy old
            for (VkFramebuffer framebuffer : framebuffers) {
                vkDestroyFramebuffer(device, framebuffer, nullptr);
            }

            info.renderPass = renderPass;
            info.width = extent.width;
            info.height = extent.height;

            size_t numViews = views.size();
            framebuffers.resize(numViews);

            for (size_t i = 0; i < numViews; i++) {

                VkImageView attachments[] = { views[i] };
                info.pAttachments = attachments;

                VkResult err = vkCreateFramebuffer(device, &info, nullptr, &(framebuffers[i]));
                if (err) { throw std::runtime_error("[Framebuffers] Couldn't create framebuffer!"); }
            }
        }
    };
};