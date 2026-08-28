module;

#include <set>
#include <vector>
#include <stdexcept>

#include <dbg.hpp>
#include <volk/volk.h>

export module Vulkan.Device;

export namespace Vulkan {

    struct Device {

        // Wrapper struct for graphics/present family indices
        struct QueueFamilyIndices {

            int graphicsFamily = 0;
            int presentFamily = 0;
        
            bool isComplete() {
                return graphicsFamily >= 0 && presentFamily >= 0;
            }
        };
    
        // Requirements
        VkInstance instance = nullptr;
        VkSurfaceKHR surface = nullptr;

        std::vector<VkPhysicalDevice> physicalDevices;
        VkPhysicalDeviceProperties properties;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device;

        // Graphics / present queue
        QueueFamilyIndices indices;
        VkQueue graphicsQueue;
        VkQueue presentQueue;

        // Create
        Device(VkInstance instance, VkSurfaceKHR surface) {

            dbg("[Vulkan][Device] Creating device");

            this->instance = instance;
            this->surface = surface;

            // Get devices and choose
            //--------------------------------------------------

            // Enumarate devices
            uint32_t deviceCount = 0;
            vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
            physicalDevices.resize(deviceCount);
            vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data());
            if (!deviceCount) { throw std::runtime_error("[Vulkan][Device] No available devices"); }

            // Select device
            for (VkPhysicalDevice& candidate : physicalDevices) {
                physicalDevice = candidate;
                indices = getQueueFamilies();
                if (indices.isComplete()) { break; }
            }

            if (physicalDevice == VK_NULL_HANDLE) {
                throw std::runtime_error("[Device] No suitable devices!");
            }

            // Get device properties
            vkGetPhysicalDeviceProperties(physicalDevice, &properties);

            // Set up queue
            //--------------------------------------------------

            float queuePriority = 1.0f;
            std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
            std::set<uint32_t> uniqueQueueFamilies = { uint32_t(indices.graphicsFamily), uint32_t(indices.presentFamily) };

            for (uint32_t queueFamily : uniqueQueueFamilies) {
                queueCreateInfos.push_back({
                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .queueFamilyIndex = queueFamily,
                    .queueCount = 1,
                    .pQueuePriorities = &queuePriority,
                });
            }

            // Create physical device
            //--------------------------------------------------

            VkPhysicalDeviceFeatures deviceFeatures{};
            const std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

            VkDeviceCreateInfo deviceCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
                .pQueueCreateInfos = queueCreateInfos.data(),
                .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
                .ppEnabledExtensionNames = deviceExtensions.data(),
                .pEnabledFeatures = &deviceFeatures,
            };

            VkResult dvResult = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &(device));
            if (dvResult != VK_SUCCESS) { throw std::runtime_error("[Device] Couldn't create logical device!"); }

            // Load device, get queues
            volkLoadDevice(device);
            vkGetDeviceQueue(device, indices.graphicsFamily, 0, &graphicsQueue);
            vkGetDeviceQueue(device, indices.presentFamily, 0, &presentQueue);
        }

        // Destroy
        ~Device() {

            dbg("[Vulkan][Surface] Destroying device");

            vkDestroyDevice(device, nullptr);
        }

        void waitIdle() {
            vkDeviceWaitIdle(device);
        }

        QueueFamilyIndices getQueueFamilies() {

            uint32_t queueFamilyCount = 0;
            
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
            std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());
        
            for (size_t i = 0; i < queueFamilyCount; ++i) {
                
                VkQueueFamilyProperties& queueFamily = queueFamilies[i];
        
                // Check for graphics support
                if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) { indices.graphicsFamily = i; }
        
                // Check for present support
                VkBool32 presentSupport = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);
                if (presentSupport) { indices.presentFamily = i; }
        
                // Break if good
                if (indices.isComplete()) { break; }
            }
        
            return indices;
        }
    };
}