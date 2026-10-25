module;
#include "Helpers/Texture.hpp"
export module Rev.Graphics.CameraTexture;
import Rev.Core.PixelFormat;
export import Rev.Graphics.Texture;
export namespace Rev::Graphics {
    // Capture only copies CPU pixels. latest() consumes the newest frame on the render thread;
    // Texture::bind records the GPU copy. No D3D command list is touched by the producer.
    struct CameraTexture {
        explicit CameraTexture(void* context) : context(context) {}
        CameraTexture(const CameraTexture&) = delete;
        CameraTexture& operator=(const CameraTexture&) = delete;
        bool upload(const unsigned char* pixels, size_t width, size_t height, Core::PixelFormat format) {
            std::scoped_lock lock(mutex);
            if (format != Core::PixelFormat::RGBA8 && format != Core::PixelFormat::BGRA8 &&
                format != Core::PixelFormat::RGBX8 && format != Core::PixelFormat::BGRX8) {
                errorText = "DirectX camera upload requires packed RGBA/BGRA/RGBX/BGRX pixels"; return false;
            }
            if (!pixels || !width || !height || width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION) {
                errorText = "Invalid camera frame"; return false;
            }
            size_t bytes = DirectX::product(DirectX::product(width, height), 4);
            pending.assign(pixels, pixels + bytes); pendingWidth = width; pendingHeight = height;
            pendingFormat = format; dirty = true; errorText.clear(); return true;
        }
        Texture* latest(Core::PixelFormat& format) {
            std::scoped_lock lock(mutex);
            if (dirty) {
                if (!texture || texture->width != pendingWidth || texture->height != pendingHeight)
                    texture = std::make_unique<Texture>(context, Texture::Params{pending.data(), pendingWidth, pendingHeight, 4});
                else texture->update(pending.data());
                pixelFormat = pendingFormat; dirty = false;
            }
            format = pixelFormat; return texture.get();
        }
        void display(Texture*) {} // In-flight GPU snapshots are retained by the canvas.
        std::string error() { std::scoped_lock lock(mutex); return errorText; }
    private:
        void* context;
        std::mutex mutex;
        std::unique_ptr<Texture> texture;
        std::vector<unsigned char> pending;
        size_t pendingWidth = 0, pendingHeight = 0;
        bool dirty = false;
        Core::PixelFormat pendingFormat = Core::PixelFormat::Null, pixelFormat = Core::PixelFormat::Null;
        std::string errorText;
    };
}
