module;

#include <cstdint>

export module Rev.Core.PixelFormat;

export namespace Rev::Core {

    enum class PixelFormat : uint32_t {
        Null,
        R8,
        RGB8,
        BGR8,
        RGBA8,
        BGRA8,
        RGBX8,
        BGRX8,
        YUY2,
        NV12
    };
}
