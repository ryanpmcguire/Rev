module;

#include <stdexcept>
#include <dbg.hpp>
#include <volk/volk.h>

export module Vulkan.RenderPass;

export namespace Vulkan {

    struct RenderPass {

        VkDevice device;

        VkAttachmentDescription colorAttachment;
        VkAttachmentReference colorAttachmentRef{};

        VkSubpassDescription subpassDesc{};
        VkSubpassDependency subpassDep{};

        VkRenderPassCreateInfo createInfo{};
        VkRenderPass renderPass;

        // Create
        RenderPass(VkDevice device, VkSurfaceFormatKHR format) {

            VkAttachmentDescription colorAttachment = {
                .format = format.format,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
            };
        
            VkAttachmentReference colorAttachmentRef = {
                .attachment = 0,
                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            };
        
            VkSubpassDescription subpassDesc = {
                .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                .colorAttachmentCount = 1, .pColorAttachments = &(this->colorAttachmentRef),
            };
        
            VkSubpassDependency subpassDep = {
                .srcSubpass = VK_SUBPASS_EXTERNAL,
                .dstSubpass = 0,
                .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            };
        
            VkRenderPassCreateInfo createInfo = {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                .attachmentCount = 1, .pAttachments = &(this->colorAttachment),
                .subpassCount = 1, .pSubpasses = &(this->subpassDesc),
                .dependencyCount = 1, .pDependencies = &(this->subpassDep),
            };
        
            this->device = device;
            this->createInfo = createInfo;
            this->subpassDep = subpassDep;
            this->subpassDesc = subpassDesc;
            this->colorAttachmentRef = colorAttachmentRef;
            this->colorAttachment = colorAttachment;
        
            VkResult err = vkCreateRenderPass(device, &(this->createInfo), nullptr, &renderPass);
            if (err) { throw std::runtime_error("[Renderpass] Couldn't create renderpass!"); }
        }

        // Destroy
        ~RenderPass() {

            dbg("[RenderPass] Destroying Renderpass");

            vkDestroyRenderPass(device, renderPass, nullptr);
        }

        void recreate() {

            vkDestroyRenderPass(device, renderPass, nullptr);

            VkResult err = vkCreateRenderPass(device, &(this->createInfo), nullptr, &renderPass);
            if (err) { throw std::runtime_error("[RenderPass] Couldn't create renderpass!"); }
        }
    };
};