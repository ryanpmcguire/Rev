module;

#include <dbg.hpp>
#include <algorithm>
#include <atomic>
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
import Rev.Core.MainDispatch;

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
        Info info;

        // Create
        Camera() {

            Roster::Get();

            // Capture is independent of its consumers. The native callback only
            // copies the sample into an immutable snapshot, swaps it into the
            // latest-frame slot and posts one coalesced signal to the main thread.
            // No consumer code, GL work or consumer lock runs on the capture thread.
            nativeCamera = new NativeCamera([this](Frame* frame) {
                try { publishSnapshot(*frame); }
                catch (const std::exception& error) { dbg("[Camera] frame dropped: %s", error.what()); }
                catch (...) { dbg("[Camera] frame dropped: unknown error"); }
                return frame;
            }, [this](std::string error) {
                nativeOffline(std::move(error));
            }, {});
        }

        Camera(const Camera&) = delete;
        Camera& operator=(const Camera&) = delete;

        // Destroy
        ~Camera() {

            *alive = nullptr;
            delete nativeCamera;
            nativeCamera = nullptr;
        }

        // Thread-safe: the newest immutable frame (nullptr before the first).
        // Its revision increases monotonically for the life of this object.
        std::shared_ptr<const Frame> latestFrame() const {
            std::scoped_lock lock(frameMutex);
            return latest;
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

        // Guards only the pointer swap of `latest` and the revision counter.
        mutable std::mutex frameMutex;
        std::shared_ptr<const Frame> latest;
        std::atomic<bool> frameSignalPending = false;
        std::shared_ptr<Camera*> alive = std::make_shared<Camera*>(this);
        std::mutex frameDemandMutex;
        std::unordered_set<void*> frameConsumers;
        uint64_t frameRevision = 0;
        Core::Dispatcher<FrameEvent> frameDispatcher;
        Core::Dispatcher<AvailabilityChanged> availabilityDispatcher;
        void frameReceived(FrameEvent& event) {
            frameDispatcher.tell(&Camera::frameReceived, event);
        }

        // Capture thread. Copy, publish, signal; never run consumers here.
        void publishSnapshot(const Frame& source) {
            const unsigned char* pixels = source.pixels;
            size_t pitch = source.pitch;
            if (!pixels && !source.data.empty()) {
                pixels = source.data.data();
                pitch = source.size.width * (source.size.channels ? source.size.channels : 4);
            }
            if (!pixels || !pitch || !source.size.width || !source.size.height) { return; }

            auto snapshot = std::make_shared<Frame>();
            snapshot->size = source.size;
            snapshot->timestamp = source.timestamp;
            snapshot->format = source.format;
            snapshot->pitch = pitch;
            snapshot->data.assign(pixels, pixels + pitch * source.size.height);
            snapshot->pixels = snapshot->data.data();

            {
                std::scoped_lock lock(frameMutex);
                snapshot->revision = ++frameRevision;
                latest = std::move(snapshot);
            }

            if (frameSignalPending.exchange(true)) { return; }
            Core::MainDispatch::instance().post([token = alive]() {
                Camera* camera = *token;
                if (!camera) { return; }
                camera->frameSignalPending = false;
                camera->signalFrame();
            });
        }

        // Main thread. Listeners receive the newest snapshot; it stays valid for
        // as long as they hold event.snapshot.
        void signalFrame() {
            std::shared_ptr<const Frame> snapshot = latestFrame();
            if (!snapshot) { return; }
            FrameEvent event{ const_cast<Frame*>(snapshot.get()), snapshot->revision, snapshot };
            frameReceived(event);
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

                info.open = false;
                info.error = error;
                info.controls.clear();

                AvailabilityChanged event;
                availabilityDispatcher.tell(&Camera::availabilityChanged, event);
                co_return;
            });
        }
    };
}
