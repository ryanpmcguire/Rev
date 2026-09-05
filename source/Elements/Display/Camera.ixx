module;

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

export module Rev.Element.Camera;

import Rev.Appearance;
import Rev.Core.Process;
import Rev.Element.Box;
import Rev.Element.Event;
import Rev.Media.Camera;
import Rev.Primitive.Video;

export namespace Rev::Element {

    namespace CameraStyles {
        Style Default = {
            .overflow = Overflow::Hide,
            .size = { 320_px, 240_px },
            .background = { .color = rgba(0, 0, 0, 1) }
        };
    }

    struct Camera : public Box {

        enum class Fit { Cover, Contain, Stretch };

        struct Params {
            size_t device = 0;
            Fit fit = Fit::Cover;
            unsigned frameRate = 30;
            bool autoStart = true;
        };

        explicit Camera(Element* parent, StyleList styles = {})
            : Camera(parent, Params{}, styles) {}

        Camera(Element* parent, Params params, StyleList styles = {})
            : Box(parent, styles, "Camera"), params(params) {

            this->styles.prepend(&CameraStyles::Default);
            video = new Primitives::Video(shared->canvas);
            if (params.autoStart) { start(); }
        }

        ~Camera() {
            Core::Process::instance().unschedule(this);
            capture.close();
            delete video;
        }

        static std::vector<Media::CameraDeviceInfo> devices() {
            return Media::CameraCapture::devices();
        }

        bool start() {
            Core::Process::instance().unschedule(this);
            bool opened = capture.open(params.device);
            if (!opened) { return false; }

            const uint64_t interval = std::max<uint64_t>(1, 1000 / std::max(1u, params.frameRate));
            Core::Process::instance().schedule(this, interval, [this](uint64_t) {
                if (!capture.isOpen()) {
                    Core::Process::instance().unschedule(this);
                    return;
                }
                if (shared && shared->event) { refresh(*shared->event); }
            });
            return true;
        }

        void stop() {
            Core::Process::instance().unschedule(this);
            capture.close();
        }

        bool isOpen() const { return capture.isOpen(); }
        std::string error() const { return capture.error(); }

        const std::vector<Media::CameraControlInfo>& controls() const {
            return capture.controls();
        }

        std::optional<Media::CameraControlInfo> control(Media::CameraControl requested) const {
            return capture.control(requested);
        }

        bool setControl(
            Media::CameraControl requested,
            double value,
            Media::CameraControlMode mode = Media::CameraControlMode::Manual
        ) {
            return capture.setControl(requested, value, mode);
        }

        bool refreshControls() { return capture.refreshControls(); }
        bool resetControls() { return capture.resetControls(); }

        template<typename Owner>
        void onControlChanged(
            Owner* owner,
            const std::function<void(Media::CameraControlEvent&)>& listener
        ) {
            capture.onControlChanged(owner, listener);
        }

        void unsubscribeControls(void* owner) { capture.unsubscribeControls(owner); }

        void setFit(Fit fit) {
            params.fit = fit;
            if (shared && shared->event) { refresh(*shared->event); }
        }

        void computePrimitives(Event& event) override {

            Box::computePrimitives(event);

            if (capture.takeFrame(frameRevision, frame)) {
                video->upload(frame.pixels.data(), frame.width, frame.height);
            }

            auto& data = *video->data;
            data.rect = rect.rounded();
            data.uv = { 0, 0, 1, 1 };
            data.opacity = resolved.opacity;

            if (frame.width == 0 || frame.height == 0 || rect.w <= 0 || rect.h <= 0) { return; }

            const float sourceAspect = static_cast<float>(frame.width) / static_cast<float>(frame.height);
            const float targetAspect = rect.w / rect.h;

            if (params.fit == Fit::Contain) {
                if (sourceAspect > targetAspect) {
                    data.rect.h = rect.w / sourceAspect;
                    data.rect.y = rect.y + (rect.h - data.rect.h) * 0.5f;
                }
                else {
                    data.rect.w = rect.h * sourceAspect;
                    data.rect.x = rect.x + (rect.w - data.rect.w) * 0.5f;
                }
            }
            else if (params.fit == Fit::Cover) {
                if (sourceAspect > targetAspect) {
                    data.uv.w = targetAspect / sourceAspect;
                    data.uv.x = (1.0f - data.uv.w) * 0.5f;
                }
                else {
                    data.uv.h = sourceAspect / targetAspect;
                    data.uv.y = (1.0f - data.uv.h) * 0.5f;
                }
            }
        }

        void draw(Event& event) override {
            Box::draw(event);
            video->draw();
        }

    private:
        Params params;
        Media::CameraCapture capture;
        Media::CameraFrame frame;
        uint64_t frameRevision = 0;
        Primitives::Video* video = nullptr;
    };
}
