module;

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

export module Rev.Media.Camera.Types;

import Rev.Core.PixelFormat;

export namespace Rev::Media::CameraTypes {

    struct DeviceInfo {
        size_t index = 0;
        std::string name;
        std::string id;
    };

    struct Frame {

        struct Event {
            Frame* frame = nullptr;
            uint64_t revision = 0;
        };

        struct Size {
            size_t width = 0;
            size_t height = 0;
            size_t channels = 0;
        };

        struct Timestamp {
            double capturedAtMs = 0;
            double receivedAtMs = 0;
            uint64_t timestamp100ns = 0;
        };

        Size size;
        Timestamp timestamp;
        Core::PixelFormat format = Core::PixelFormat::Null;

        Frame** published = nullptr;
        std::mutex* mutex = nullptr;
        uint64_t revision = 0;

        std::vector<unsigned char> data;

        Frame copy() {
            
            if (!mutex || !published) {
                return {
                    .size = size,
                    .timestamp = timestamp,
                    .format = format,
                    .revision = revision,
                    .data = data
                };
            }

            std::scoped_lock lock(*mutex);
            if (*published != this) { return {}; }

            return {
                .size = size,
                .timestamp = timestamp,
                .format = format,
                .revision = revision,
                .data = data
            };
        }
    };

    struct Setting {

        enum class Type {

            Null,

            Exposure, Iris,
            Focus, Zoom,
            Gain, Gamma,
            Brightness, Contrast,
            Hue, Saturation, WhiteBalance,
            Sharpness, BacklightCompensation
        };

        enum class Mode {
            Null, Disabled,
            Manual, Automatic
        };

        std::string name;

        Type control = Type::Null;
        Mode mode = Mode::Null;

        long min = 0, max = 0;
        long val = 0, def = 0;
        
        long step = 0;
        bool supportsManual = false;
        bool supportsAutomatic = false;
    };
}
