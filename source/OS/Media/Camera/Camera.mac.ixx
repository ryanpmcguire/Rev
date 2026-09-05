module;

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <optional>
#include <functional>

export module Rev.Media.Camera;

import Rev.Core.Dispatcher;

export namespace Rev::Media {
    struct CameraDeviceInfo { size_t index = 0; std::string name; std::string id; };
    struct CameraFrame {
        size_t width = 0, height = 0;
        uint64_t timestamp100ns = 0;
        std::vector<unsigned char> pixels;
    };
    enum class CameraControl {
        Exposure, Focus, Iris, Zoom, Gain, Gamma, Brightness, Contrast,
        Hue, Saturation, Sharpness, WhiteBalance, BacklightCompensation
    };
    enum class CameraControlMode { Manual, Automatic };
    struct CameraControlInfo {
        CameraControl control = CameraControl::Exposure;
        std::string name;
        long minimum = 0, maximum = 0, step = 1, defaultValue = 0, value = 0;
        bool supportsManual = false, supportsAutomatic = false;
        CameraControlMode mode = CameraControlMode::Manual;
    };
    struct CameraControlEvent { CameraControlInfo info; };
    struct CameraCapture {
        static std::vector<CameraDeviceInfo> devices() { return {}; }
        bool open(size_t = 0) { return false; }
        void close() {}
        bool isOpen() const { return false; }
        std::string error() const { return "Camera capture is not implemented for macOS"; }
        bool takeFrame(uint64_t&, CameraFrame&) { return false; }
        const std::vector<CameraControlInfo>& controls() const { return controlCache; }
        std::optional<CameraControlInfo> control(CameraControl) const { return std::nullopt; }
        template<typename Owner>
        void onControlChanged(Owner* owner, const std::function<void(CameraControlEvent&)>& listener) {
            controlDispatcher.listen(&CameraCapture::controlChanged, owner, listener);
        }
        void unsubscribeControls(void* owner) { controlDispatcher.unsubscribe(owner); }
        bool refreshControls(bool = true) { return false; }
        bool setControl(CameraControl, double, CameraControlMode = CameraControlMode::Manual) { return false; }
        bool resetControls() { return false; }
    private:
        std::vector<CameraControlInfo> controlCache;
        Core::Dispatcher<CameraControlEvent> controlDispatcher;
        void controlChanged(CameraControlEvent& event) {
            controlDispatcher.tell(&CameraCapture::controlChanged, event);
        }
    };
}
