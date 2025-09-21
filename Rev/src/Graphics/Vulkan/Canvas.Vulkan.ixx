module;

export module Rev.CanvasVk;

import Vulkan.Instance;
import Vulkan.Surface;

export namespace Rev {

    struct Canvas {

        struct Flags {
            bool resize = true;
            bool record = true;
        };

        void* window = nullptr;
        inline static Vulkan::Instance* vulkan = nullptr;
        Vulkan::Surface* surface = nullptr;

        Flags flags;

        // Create
        Canvas(void* window = nullptr) {

            if (!vulkan) { vulkan = new Vulkan::Instance(); }
            surface = new Vulkan::Surface(vulkan->instance, window);
        }

        // Destroy
        ~Canvas() {
            
        }

        void draw() {

            if (flags.resize) { surface->resize(); flags.resize = false; flags.record = true; }
            if (flags.record) { surface->record(); flags.record = false; }

            surface->draw();
        }
    };
};