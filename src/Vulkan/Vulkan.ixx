module;

#include <vector>
#include <stdexcept>

#include <dbg.hpp>
#include <volk/volk.h>
#include <GLFW/glfw3.h>

export module Vulkan.Instance;

export namespace Vulkan {

    struct Instance {

        // Instance
        VkApplicationInfo appInfo = {};
        VkInstanceCreateInfo createInfo;
        VkInstance instance;

        // Validation layers we're using
        std::vector<const char*> validationLayers = {
            "VK_LAYER_KHRONOS_validation"
        };

        // Create
        Instance() {

            dbg("[Vulkan] Creating Instance");

            // Initialize volk
            VkResult volkErr = volkInitialize();
            if (volkErr) { throw std::runtime_error("[Volk] Failed to initialize volk"); }

            // Get glfw extensions
            uint32_t glfwExtensionCount = 0;
            const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

            // Fill out app info
            appInfo = {
                .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                .pNext = nullptr,
                .pApplicationName = "Hello World",
                .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
                .pEngineName = "Hello World Engine",
                .engineVersion = VK_MAKE_VERSION(1, 0, 0),
                .apiVersion = VK_API_VERSION_1_3
            };

            // Fill out create info
            createInfo = {
                .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                .pApplicationInfo = &appInfo,
                .enabledLayerCount = uint32_t(validationLayers.size()),
                .ppEnabledLayerNames = validationLayers.data(),
                .enabledExtensionCount = glfwExtensionCount,
                .ppEnabledExtensionNames = glfwExtensions
            };

            // Create vulkan instance
            VkResult instanceErr = vkCreateInstance(&createInfo, nullptr, &instance);
            if (instanceErr) { throw std::runtime_error("[Vulkan] Failed to create instance"); }

            // Load volk functions
            volkLoadInstance(instance);
        }

        // Destroy
        ~Instance() {

            dbg("[Vulkan] Destroying instance");

            vkDestroyInstance(instance, nullptr);
        }
    };
}