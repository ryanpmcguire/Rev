module;

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

export module Rev.Media.Camera;

export import Rev.Media.Camera.Types;
import Rev.Media.NativeCamera;
import Rev.Core.Dispatcher;
import Rev.Core.Promise;

export namespace Rev::Media {

    // Shared camera policy. NativeCamera owns only platform resources and driver I/O.
    struct Camera {

        struct AvailabilityChanged {};

        struct DevicesChanged {};

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

        struct Roster final {

            struct AsyncState final {
                std::mutex mutex;
                Roster* roster = nullptr;
            };

            std::mutex mutex;
            std::vector<DeviceInfo> knownDevices;
            Core::Dispatcher<DevicesChanged> dispatcher;
            NativeCamera* monitor = nullptr;
            std::shared_ptr<AsyncState> asyncState;

            Roster(const Roster&) = delete;
            Roster(Roster&&) = delete;
            Roster& operator=(const Roster&) = delete;
            Roster& operator=(Roster&&) = delete;

            Roster() {
                asyncState = std::make_shared<AsyncState>();
                asyncState->roster = this;
                knownDevices = NativeCamera::devices();
                monitor = new NativeCamera({}, {}, [this]() {
                    nativeDevicesChanged();
                });
            }

            ~Roster() {
                {
                    std::scoped_lock lock(asyncState->mutex);
                    asyncState->roster = nullptr;
                }

                delete monitor;
                monitor = nullptr;
            }

            static Roster& Get() {
                static Roster roster;
                return roster;
            }

            std::vector<DeviceInfo> devices() {
                std::scoped_lock lock(mutex);
                return knownDevices;
            }

            template<typename Owner>
            void listen(
                Owner* owner,
                const std::function<void(DevicesChanged&)>& listener
            ) {
                dispatcher.listen(&Roster::changed, owner, listener);
            }

            void unsubscribe(void* owner) {
                dispatcher.unsubscribe(owner);
            }

            void changed(DevicesChanged&) {}

            void nativeDevicesChanged() {
                std::shared_ptr<AsyncState> state = asyncState;

                Core::Async::Do([state]() -> Core::Promise<void> {
                    using namespace std::chrono_literals;

                    co_await Core::Async::delay(100ms);
                    std::vector<DeviceInfo> current = NativeCamera::devices();
                    co_await Core::Async::mainThread();

                    std::scoped_lock stateLock(state->mutex);
                    Roster* roster = state->roster;

                    if (roster == nullptr) {
                        co_return;
                    }

                    {
                        std::scoped_lock rosterLock(roster->mutex);

                        if (Camera::sameDevices(roster->knownDevices, current)) {
                            co_return;
                        }

                        roster->knownDevices = std::move(current);
                    }

                    DevicesChanged event;
                    roster->dispatcher.tell(&Roster::changed, event);
                });
            }
        };

        NativeCamera* nativeCamera = nullptr;
        Frame* frame = nullptr;
        Info info;

        // Create
        Camera() {

            Roster::Get();

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
            }, [this](std::string error) {
                nativeOffline(std::move(error));
            }, {});
            
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

        static std::vector<DeviceInfo> devices() { return Roster::Get().devices(); }

        bool open(size_t deviceIndex = 0) {
            close();
            info.open = nativeCamera->open(deviceIndex);
            info.error = nativeCamera->error();
            if (!info.open) { return false; }
            refreshControls(false);

            {
                std::scoped_lock lock(frameDemandMutex);
                if (!frameConsumers.empty()) {
                    nativeCamera->start();
                }
            }

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
        void onAvailability(
            Owner* owner,
            const std::function<void(AvailabilityChanged&)>& listener
        ) {
            availabilityDispatcher.listen(&Camera::availabilityChanged, owner, listener);
        }

        void unsubscribeAvailability(void* owner) {
            availabilityDispatcher.unsubscribe(owner);
        }

        template<typename Owner>
        void onDevicesChanged(
            Owner* owner,
            const std::function<void(DevicesChanged&)>& listener
        ) {
            Roster::Get().listen(owner, listener);
        }

        void unsubscribeDevices(void* owner) {
            Roster::Get().unsubscribe(owner);
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

        void acquireFrames(void* owner) {
            if (owner == nullptr) { return; }

            std::scoped_lock lock(frameDemandMutex);
            frameConsumers.insert(owner);

            if (info.open) {
                nativeCamera->start();
            }
        }

        void releaseFrames(void* owner) {
            std::scoped_lock lock(frameDemandMutex);
            frameConsumers.erase(owner);

            if (frameConsumers.empty()) {
                nativeCamera->stop();
            }
        }

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
        std::mutex frameDemandMutex;
        std::unordered_set<void*> frameConsumers;
        uint64_t frameRevision = 0;
        Core::Dispatcher<FrameEvent> frameDispatcher;
        Core::Dispatcher<AvailabilityChanged> availabilityDispatcher;
        void frameReceived(FrameEvent& event) {
            frameDispatcher.tell(&Camera::frameReceived, event);
        }
        void controlTelemetry(SettingType&) {
            ++info.controlsRevision;
        }

        void availabilityChanged(AvailabilityChanged&) {}

        static bool sameDevices(
            const std::vector<DeviceInfo>& left,
            const std::vector<DeviceInfo>& right
        ) {
            if (left.size() != right.size()) {
                return false;
            }

            for (size_t index = 0; index < left.size(); ++index) {
                if (
                    left[index].index != right[index].index
                    || left[index].name != right[index].name
                    || left[index].id != right[index].id
                ) {
                    return false;
                }
            }

            return true;
        }

        void nativeOffline(std::string error) {
            Core::Async::Do([
                this,
                error = std::move(error)
            ]() mutable -> Core::Promise<void> {
                co_await Core::Async::mainThread();

                info.open = false;
                info.error = error;
                info.controls.clear();

                AvailabilityChanged event;
                availabilityDispatcher.tell(&Camera::availabilityChanged, event);
            });
        }
    };
}
