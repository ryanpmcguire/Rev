module;
#include "Helpers/Canvas.hpp"
export module Rev.Graphics.Canvas;
import Rev.NativeWindow;
export import Rev.Graphics.Pipeline;
export import Rev.Graphics.UniformBuffer;
export import Rev.Graphics.FrameBuffer;
export import Rev.Graphics.CommandBuffer;
export namespace Rev::Graphics {
    struct Canvas : DirectX::Canvas {
        NativeWindow* window = nullptr;
        CommandBuffer* commandBuffer = nullptr;
        explicit Canvas(NativeWindow* window = nullptr)
            : DirectX::Canvas(window ? window->handle : nullptr), window(window) {}
        void beginFrame() {
            if (!window) throw std::logic_error("[DirectX] Canvas needs a window");
            DirectX::Canvas::beginFrame(window->size.w, window->size.h, window->scale);
        }
    };
}
