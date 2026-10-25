module;

#include <glew/glew.h>
#include <mutex>
#include <string>
#include <vector>

export module Rev.Graphics.CameraTexture;

import Rev.Core.PixelFormat;
import Rev.Graphics.Texture;
import Rev.NativeWindow;

export namespace Rev::Graphics {

    // This object is called by the capture callback thread.  Its textures are
    // shared with the canvas, while the display thread only reads `latest()` and
    // binds the returned pointer.
    struct CameraTexture {

        struct Slot {
            Texture* texture = nullptr;
            size_t width = 0;
            size_t height = 0;
        };

        explicit CameraTexture(void* windowContext)
            : window(static_cast<NativeWindow*>(windowContext)) {
            // Construction happens with the canvas context current on the UI
            // thread. WGL establishes the sharing relationship here; capture
            // only claims this context before issuing an upload.
            worker = NativeWindow::createSharedContext(window);
            if (!worker) { errorText = "Could not create a shared OpenGL capture context"; }
        }

        ~CameraTexture() {
            std::scoped_lock lock(mutex);
            for (Slot& slot : slots) { delete slot.texture; }
            slots.clear();
            if (worker) {
                worker->destroy();
                delete worker;
            }
        }

        bool upload(const unsigned char* pixels, size_t width, size_t height, Core::PixelFormat format) {
            std::scoped_lock lock(mutex);
            if (!pixels || !width || !height || !window) {
                errorText = "Camera supplied no frame pixels";
                return false;
            }
            if (!worker) {
                errorText = "Could not create a shared OpenGL capture context";
                return false;
            }
            if (!worker->makeCurrent()) {
                errorText = "Could not make the shared OpenGL capture context current";
                return false;
            }

            Slot* slot = nextSlot(width, height);
            if (!slot) {
                errorText = "No free camera texture slot";
                worker->release();
                return false;
            }
            if (!slot->texture) {
                slot->texture = Texture::createInCurrentContext(window, {
                    .data = const_cast<unsigned char*>(pixels), .width = width, .height = height, .channels = 4
                });
                slot->width = width;
                slot->height = height;
            }
            else {
                slot->texture->update(pixels);
            }
            glFlush();
            worker->release();
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
        NativeWindow::SharedContext* worker = nullptr;
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
