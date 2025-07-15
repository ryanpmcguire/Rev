module;

#define SK_VULKAN

#include <volk/volk.h>

#include <include/core/SkCanvas.h>
#include <include/core/SkSurface.h>
#include <include/core/SkPaint.h>
#include <include/gpu/vk/GrVkBackendContext.h>
#include <include/gpu/vk/GrVkExtensions.h>
#include <include/gpu/vk/GrVkTypes.h>
#include <include/gpu/GrDirectContext.h>

export module Skia;

import Vulkan.Instance;
import Vulkan.Device;
import Vulkan.Surface;

export namespace Skia {

    struct Skia {

        GrVkBackendContext context;

        Skia(Vulkan::Instance* instance, Vulkan::Device* device, Vulkan::Surface* surface) {

            context = {

                .fInstance = instance->instance,
                .fPhysicalDevice = device->physicalDevice,
                .fDevice = device->device,
                .fQueue = device->graphicsQueue,
                .fGraphicsQueueIndex = uint32_t(device->indices.graphicsFamily),
                .fMaxAPIVersion = VK_API_VERSION_1_2
            };

            sk_sp<GrDirectContext> skiaContext = GrDirectContext::MakeVulkan(context);
        }

        ~Skia() {

        }

        void draw() {

            SkImageInfo info = SkImageInfo::MakeN32Premul(400, 400);
            auto surface = SkSurface::MakeRaster(info);
            SkCanvas* canvas = surface->getCanvas();

            SkPaint paint;
            paint.setColor(SK_ColorBLUE);
            canvas->drawCircle(200, 200, 100, paint);

            surface->flush();

            return;
        }
    };
};