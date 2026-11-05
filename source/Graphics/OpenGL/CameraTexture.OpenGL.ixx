module;

#include <glew/glew.h>
#include <stdexcept>
#include <mutex>
#include <string>
#include <vector>

export module Rev.Graphics.CameraTexture;

import Rev.Core.PixelFormat;
import Rev.Graphics.Texture;
import Rev.NativeWindow;

export namespace Rev::Graphics {

    // Called only on the UI/render thread that owns the window's GL context,
    // from the camera's latest immutable snapshot. Capture threads never touch
    // GL (no shared worker context, no wglMakeCurrent off the UI thread).
    struct CameraTexture {

        struct Slot {
            Texture* texture = nullptr;
            size_t width = 0;
            size_t height = 0;
        };

        explicit CameraTexture(void* windowContext)
            : window(static_cast<NativeWindow*>(windowContext)) {}

        ~CameraTexture() {
            std::scoped_lock lock(mutex);
            for (Slot& slot : slots) { delete slot.texture; }
            slots.clear();
        }

        // `pitch` = bytes per source row (0 = unknown, assumed tight). The upload
        // assumes tightly packed 4-byte rows; a padded pitch would shear the
        // image, so it is refused loudly instead (guard, 2026-11-04).
        bool upload(const unsigned char* pixels, size_t width, size_t height, Core::PixelFormat format,
                    size_t pitch = 0) {
            std::scoped_lock lock(mutex);
            if (!pixels || !width || !height || !window) {
                errorText = "Camera supplied no frame pixels";
                return false;
            }
            if (pitch && pitch != width * 4) {
                errorText = "Camera frame pitch " + std::to_string(pitch) + " != width*4 ("
                    + std::to_string(width * 4) + "); refusing a sheared upload";
                return false;
            }
            try { NativeWindow::requireContext(window, "Camera texture upload"); }
            catch (const std::exception& error) {
                errorText = error.what();
                return false;
            }

            Slot* slot = nextSlot(width, height);
            if (!slot) {
                errorText = "No free camera texture slot";
                return false;
            }
            if (!slot->texture) {
                slot->texture = Texture::createInCurrentContext(window, {
                    .data = const_cast<unsigned char*>(pixels), .width = width, .height = height, .channels = 4,
                    .filter = Texture::Filter::Trilinear
                });
                slot->width = width;
                slot->height = height;
            }
            else {
                slot->texture->update(pixels);
            }
            published = slot->texture;
            pixelFormat = format;
            errorText.clear();
            return true;
        }

        std::string error() {
            std::scoped_lock lock(mutex);
            return errorText;
        }

        Texture* latest(Core::PixelFormat& format) {
            std::scoped_lock lock(mutex);
            format = pixelFormat;
            return published;
        }

        void display(Texture* texture) {
            std::scoped_lock lock(mutex);
            displayed = texture;
        }

        NativeWindow* window = nullptr;
        std::mutex mutex;
        std::vector<Slot> slots = { Slot{}, Slot{}, Slot{} };
        Texture* published = nullptr;
        Texture* displayed = nullptr;
        Core::PixelFormat pixelFormat = Core::PixelFormat::Null;
        std::string errorText;

        Slot* nextSlot(size_t width, size_t height) {
            for (Slot& slot : slots) {
                if (!slot.texture) { return &slot; }
                if (slot.texture != displayed && slot.texture != published &&
                    slot.width == width && slot.height == height) { return &slot; }
            }
            for (Slot& slot : slots) {
                if (!slot.texture || slot.texture != displayed) {
                    if (slot.texture && (slot.width != width || slot.height != height)) {
                        Texture::destroyInCurrentContext(slot.texture);
                        slot.texture = nullptr;
                    }
                    return &slot;
                }
            }
            return nullptr;
        }
    };
}
