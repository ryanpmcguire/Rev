module;

#include <functional>
#include <string>
#include <vector>

export module Rev.Media.NativeCamera;

import Rev.Media.Camera.Types;

export namespace Rev::Media {
    struct NativeCamera {
        NativeCamera(
            std::function<CameraTypes::Frame*(CameraTypes::Frame*)>,
            std::function<void(std::string)>,
            std::function<void()>
        ) {}
        NativeCamera(const NativeCamera&) = delete;
        NativeCamera& operator=(const NativeCamera&) = delete;
        static std::vector<CameraTypes::DeviceInfo> devices() { return {}; }
        bool open(size_t = 0) { return false; }
        void close() {}
        bool isOpen() const { return false; }
        std::string error() const { return "Camera capture is not implemented for Linux"; }
        std::vector<CameraTypes::Setting> readControls() { return {}; }
        std::string mediaFormat() const { return "unavailable (platform)"; }
        bool writeControl(CameraTypes::Setting::Type, long, CameraTypes::Setting::Mode, CameraTypes::Setting&) {
            return false;
        }
    };
}
