module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

export module Rev.Media.Camera;

export import Rev.Media.Camera.Types;
import Rev.Media.NativeCamera;
import Rev.Core.Dispatcher;

export namespace Rev::Media {

    // Shared camera policy. NativeCamera owns only platform resources and driver I/O.
    struct Camera {

        using DeviceInfo = CameraTypes::DeviceInfo;
        using Frame = CameraTypes::Frame;
        using FrameEvent = Frame::Event;
        using Setting = CameraTypes::Setting;
        using SettingType = Setting::Type;
        using SettingMode = Setting::Mode;

        struct Control : Setting {
            Camera* camera = nullptr;
            Core::Dispatcher<SettingType> dispatcher;

            bool set(double requestedValue, SettingMode requestedMode = SettingMode::Manual) {
                if (!camera || !camera->info.open) { return false; }
                if (requestedMode == SettingMode::Manual && !supportsManual) { return false; }
                if (requestedMode == SettingMode::Automatic && !supportsAutomatic) { return false; }

                long requested = static_cast<long>(std::llround(requestedValue));
                requested = std::clamp(requested, min, max);
                if (step > 1) {
                    requested = min + static_cast<long>(std::llround(
                        static_cast<double>(requested - min) / step
                    )) * step;
                    requested = std::clamp(requested, min, max);
                }

                Setting actual;
                if (!camera->nativeCamera->writeControl(control, requested, requestedMode, actual)) { return false; }
                static_cast<Setting&>(*this) = actual;
                notify();
                return true;
            }

            bool reset() {
                return set(def, supportsManual ? SettingMode::Manual : SettingMode::Automatic);
            }

            template<typename Owner>
            void onChange(Owner* owner, const std::function<void(SettingType&)>& listener) {
                dispatcher.listen(&Control::changed, owner, listener);
            }

            void unsubscribe(void* owner) { dispatcher.unsubscribe(owner); }

            void changed(SettingType&) {}

            void notify() {
                SettingType changedControl = control;
                dispatcher.tell(&Control::changed, changedControl);
            }
        };

        struct Info {
            bool open = false;
            std::string error;
            uint64_t controlsRevision = 0;
            std::vector<Control> controls;
        };

        NativeCamera* nativeCamera = nullptr;
        Frame* frame = nullptr;
        Info info;

        // Create
        Camera() {

            nativeCamera = new NativeCamera([this](Frame* frame) {

                Frame* reusable = nullptr;
                uint64_t revision = 0;

                {
                    std::scoped_lock lock(frameMutex);
                    reusable = this->frame;
                    this->frame = frame;
                    frame->mutex = &frameMutex;
                    frame->published = &this->frame;
                    revision = ++frameRevision;
                    frame->revision = revision;
                }

                FrameEvent event{ frame, revision };
                frameReceived(event);

                return reusable;
            });
            
            frame = new Frame;
            frame->mutex = &frameMutex;
            frame->published = &frame;
        }

        Camera(const Camera&) = delete;
        Camera& operator=(const Camera&) = delete;

        // Destroy
        ~Camera() {

            delete nativeCamera;
            delete frame;

            nativeCamera = nullptr;
            frame = nullptr;
        }

        static std::vector<DeviceInfo> devices() { return NativeCamera::devices(); }

        bool open(size_t deviceIndex = 0) {
            close();
            info.open = nativeCamera->open(deviceIndex);
            info.error = nativeCamera->error();
            if (!info.open) { return false; }
            refreshControls(false);
            return true;
        }

        void close() {
            // close joins/detaches native callbacks before shared state is cleared.
            nativeCamera->close();
            info.open = false;
            info.error = nativeCamera->error();
            info.controls.clear();
            // Keep revisions monotonic across restarts for existing consumers.
        }

        template<typename Owner>
        void onFrame(
            Owner* owner,
            const std::function<void(FrameEvent&, Frame*)>& listener
        ) {
            frameDispatcher.listen(&Camera::frameReceived, owner, [listener](FrameEvent& event) {
                listener(event, event.frame);
            });
        }

        void unsubscribeFrames(void* owner) { frameDispatcher.unsubscribe(owner); }

        Control* control(SettingType requested) {
            auto found = std::find_if(
                info.controls.begin(),
                info.controls.end(),
                [requested](const Control& info) { return info.control == requested; }
            );
            if (found == info.controls.end()) { return nullptr; }
            return &*found;
        }

        const Control* control(SettingType requested) const {
            return const_cast<Camera*>(this)->control(requested);
        }

        bool refreshControls(bool dispatchChanges = true) {
            info.open = nativeCamera->isOpen();
            info.error = nativeCamera->error();
            if (!info.open) { return false; }

            auto refreshed = nativeCamera->readControls();
            bool any = !refreshed.empty();
            for (const Setting& settingInfo : refreshed) {
                Control* existing = control(settingInfo.control);
                if (existing) {
                    static_cast<Setting&>(*existing) = settingInfo;
                    if (dispatchChanges) {
                        existing->notify();
                    }
                    continue;
                }
                Control added;
                static_cast<Setting&>(added) = settingInfo;
                added.camera = this;
                added.onChange(this, [this](SettingType& changedControl) {
                    controlTelemetry(changedControl);
                });
                info.controls.push_back(std::move(added));
            }
            return any;
        }

        mutable std::mutex frameMutex;
        uint64_t frameRevision = 0;
        Core::Dispatcher<FrameEvent> frameDispatcher;
        void frameReceived(FrameEvent& event) {
            frameDispatcher.tell(&Camera::frameReceived, event);
        }
        void controlTelemetry(SettingType&) {
            ++info.controlsRevision;
        }
    };
}
