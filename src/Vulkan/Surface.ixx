module;

#include <vector>
#include <stdexcept>

#include <dbg.hpp>
#include <volk/volk.h>
#include <GLFW/glfw3.h>

export module Vulkan.Surface;

export namespace Vulkan {

    struct Surface {

        GLFWwindow* window = nullptr;
        VkInstance instance = nullptr;
        VkSurfaceKHR surface = nullptr;

        // Create
        Surface(VkInstance instance, GLFWwindow* window) {

            dbg("[Vulkan][Surface] Creating surface");

            this->instance = instance;
            this->window = window;
        }

        // Destroy
        ~Surface() {

            dbg("[Vulkan][Surface] Destroying surface");
        }
    };
}