module;

#include <algorithm>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <memory>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>

#include <windows.h>
#include <cfgmgr32.h>

#include <dbg.hpp>

#pragma comment(lib, "Cfgmgr32.lib")

export module Rev.SerialClient;

import Rev.Transport;
import Rev.Core.Promise;
import Rev.UsbProxy;

export namespace Rev {

    // ------------------------------------------------------------------
    // SerialClient  — the Win32 serial (USB-CDC virtual COM) implementation of
    // the Rev::Transport byte-pipe. A worker owns the port and enqueues bytes via
    // the inherited fire* helpers, and the main-thread pump() (from Transport)
    // drains the queue so every listener runs single-threaded.
    //
    // One owner surface, two port backends (Thesis clauses 6 and 7):
    //   - Win32:    CreateFile/ReadFile/WriteFile + CM_Register_Notification.
    //   - UsbProxy: the broker, presented by Rev::UsbProxy as an OS-shaped
    //               port (open/close/read/write/notify/exclusive, raw bytes).
    // SerialClient never sees broker rules (sessions, hex, JSON, credit,
    // leases, reconnect). Owners use only:
    //   onDevice  selectPort  connect/disconnect  send  sendBulk+onBulkDone
    //   lease/release
    // and their code is identical in both modes.
    //
    //   Rev::SerialClient c;
    //   c.setProxy({.enabled = false});         // or true + channel
    //   c.onDevice([&](auto& e) { if (e.state == Available) c.connect("", 0); });
    //   c.selectPort("COM3", 115200);
    //
    // Reads are BYTE-oriented and fragmented — a read may return a single byte
    // or a partial line. The adapter's decoder reassembles lines; this class
    // never assumes a whole line/packet per read.
    // ------------------------------------------------------------------

    struct SerialClient : public Transport {

        struct ProxyOptions {
            bool enabled = false;
            std::string serviceScript;
            std::string python = "python";
            // Broker channel (e.g. "serial1") in proxy mode.
            std::string logicalDevice;
        };

        struct DeviceEvent {
            enum class State { Available, Unavailable, Changed };
            State state = State::Changed;
            std::string port;           // selected port
            int vid = -1;
            int pid = -1;
            std::vector<PortInfo> ports; // current inventory (always filled)
            bool inventory = false;     // this event is an inventory change
            bool leased = false;        // exclusive access held by anyone
            bool mine = false;          // ... by this client
            std::string reason;
            bool seed = false;
        };

        struct BulkDoneEvent {
            uint64_t id = 0;
            size_t written = 0;
        };

        // -- Construction / destruction ----------------------------------

        SerialClient() {
            tasks_->owner = this;
        }

        ~SerialClient() override {
            dbg("[SerialClient] Shutting down");
            {
                std::lock_guard lock(tasks_->mutex);
                tasks_->owner = nullptr;
            }
            stopPump();
            backend_.reset();

            stopRequested_ = true;
            if (worker_.joinable()) {
                CancelSynchronousIo(worker_.native_handle());
                worker_.join();
            }
        }

        // -- Owner surface -----------------------------------------------

        void onDevice(std::function<void(DeviceEvent&)> f) { deviceListeners_.push_back(std::move(f)); }
        void onBulkDone(std::function<void(BulkDoneEvent&)> f) { bulkListeners_.push_back(std::move(f)); }

        // Choose the port backend. Device events start flowing at once (in
        // either mode), so owners learn availability before connect().
        void setProxy(ProxyOptions options) {
            proxyOptions_ = std::move(options);
            backend_.reset();
            if (proxyOptions_.enabled) backend_ = std::make_unique<ProxyBackend>(*this, proxyOptions_);
            else backend_ = std::make_unique<Win32Backend>(*this);
            startPump();
        }
        const ProxyOptions& proxy() const noexcept { return proxyOptions_; }

        // Select the physical port (and optionally its settings). Never opens it.
        void selectPort(const std::string& port, int baud = 0, std::optional<bool> dtr = {}) {
            ensureBackend();
            {
                std::lock_guard lock(selectionMutex_);
                selectedPort_ = port;
            }
            if (baud > 0) selectedBaud_ = baud;
            if (dtr) dtrEnabled_ = dtr;
            backend_->select(port, baud, dtr);
        }

        // Open the data path to the selected port (or `port`, if given).
        void connect(std::string port, int baud) override {
            if (running) return;
            ensureBackend();
            if (!port.empty()) selectPort(port, baud);
            else if (baud > 0) selectedBaud_ = baud;
            backend_->open();
        }

        void send(const std::string& msg) override {
            if (!running) {
                dbg("[SerialClient] send() on closed port");
                return;
            }
            backend_->write(msg, 0);
        }

        // One logical transfer; onBulkDone fires when every byte was written.
        // Returns the transfer id (0 if the port is not open).
        uint64_t sendBulk(const std::string& bytes) {
            if (!running) {
                dbg("[SerialClient] sendBulk() on closed port");
                return 0;
            }
            const uint64_t id = ++nextBulk_;
            backend_->write(bytes, id);
            return id;
        }

        // Exclusive access (tools). The result arrives as a Changed device
        // event with reason "lease_granted" / "lease_denied".
        void lease(const std::string& owner, int priority = 0, int timeoutMs = 0) {
            ensureBackend();
            backend_->acquire(owner, priority, timeoutMs);
        }
        void release() {
            if (backend_) backend_->release();
        }

        // Hold a modem-control line at a stable level for devices whose USB
        // CDC implementation requires carrier assertion. This is configured
        // once with the DCB; it never performs a reset pulse.
        void setDtrEnabled(bool enabled) { dtrEnabled_ = enabled; }

        void disconnect() override {
            if (backend_) backend_->close();
        }

        // -- Enumeration -------------------------------------------------

        // Every serial port the OS currently exposes, by name (e.g. "COM3").
        // Reads HKLM\HARDWARE\DEVICEMAP\SERIALCOMM.
        static std::vector<std::string> enumeratePorts() {

            std::vector<std::string> ports;

            HKEY key;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                              "HARDWARE\\DEVICEMAP\\SERIALCOMM",
                              0, KEY_READ, &key) != ERROR_SUCCESS) {
                return ports;   // no serial ports present
            }

            char  name[256];
            char  data[256];
            DWORD index = 0;

            while (true) {
                DWORD nameLen = sizeof(name);
                DWORD dataLen = sizeof(data);
                DWORD type    = 0;

                LONG r = RegEnumValueA(key, index++, name, &nameLen, nullptr,
                                       &type, reinterpret_cast<BYTE*>(data), &dataLen);
                if (r == ERROR_NO_MORE_ITEMS) { break; }
                if (r == ERROR_SUCCESS && type == REG_SZ) {
                    ports.emplace_back(data);   // value data is the "COMx" name
                }
            }

            RegCloseKey(key);
            return ports;
        }

        // Every active serial port with its USB VID/PID when known (-1 otherwise).
        static std::vector<PortInfo> enumeratePortDetails() {
            std::vector<PortInfo> result;
            for (const std::string& port : enumeratePorts()) result.push_back({port, -1, -1, {}});
            HKEY usb;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                    "SYSTEM\\CurrentControlSet\\Enum\\USB", 0, KEY_READ, &usb) != ERROR_SUCCESS)
                return result;
            char device[256]; DWORD deviceIndex = 0;
            while (true) {
                DWORD deviceLength = sizeof(device);
                const LONG deviceResult = RegEnumKeyExA(usb, deviceIndex++, device,
                    &deviceLength, nullptr, nullptr, nullptr, nullptr);
                if (deviceResult == ERROR_NO_MORE_ITEMS) break;
                if (deviceResult != ERROR_SUCCESS) continue;
                unsigned vid = 0, pid = 0;
                if (std::sscanf(device, "VID_%4x&PID_%4x", &vid, &pid) != 2) continue;
                HKEY deviceKey;
                if (RegOpenKeyExA(usb, device, 0, KEY_READ, &deviceKey) != ERROR_SUCCESS) continue;
                char instance[256]; DWORD instanceIndex = 0;
                while (true) {
                    DWORD instanceLength = sizeof(instance);
                    const LONG instanceResult = RegEnumKeyExA(deviceKey, instanceIndex++, instance,
                        &instanceLength, nullptr, nullptr, nullptr, nullptr);
                    if (instanceResult == ERROR_NO_MORE_ITEMS) break;
                    if (instanceResult != ERROR_SUCCESS) continue;
                    const std::string parameters = std::string(instance) + "\\Device Parameters";
                    HKEY parameterKey;
                    if (RegOpenKeyExA(deviceKey, parameters.c_str(), 0, KEY_READ,
                            &parameterKey) != ERROR_SUCCESS) continue;
                    char port[64]; DWORD portLength = sizeof(port), type = 0;
                    if (RegQueryValueExA(parameterKey, "PortName", nullptr, &type,
                            reinterpret_cast<BYTE*>(port), &portLength) == ERROR_SUCCESS &&
                        type == REG_SZ && portLength > 1) {
                        for (PortInfo& info : result)
                            if (info.port == port) {
                                info.vid = static_cast<int>(vid);
                                info.pid = static_cast<int>(pid);
                                info.serial = instance;
                            }
                    }
                    RegCloseKey(parameterKey);
                }
                RegCloseKey(deviceKey);
            }
            RegCloseKey(usb);
            std::sort(result.begin(), result.end(),
                [](const PortInfo& a, const PortInfo& b) { return a.port < b.port; });
            return result;
        }

        // Enumerate only USB serial ports matching a device family. Protocol
        // clients must not probe unrelated peripherals at arbitrary baud rates.
        static std::vector<std::string> enumerateUsbPorts(uint16_t vid, uint16_t pid) {
            std::vector<std::string> ports;
            for (const PortInfo& info : enumeratePortDetails())
                if (info.vid == vid && info.pid == pid) ports.push_back(info.port);
            return ports;
        }

    private:

        // -- Port backends -------------------------------------------------

        struct Backend {
            virtual ~Backend() = default;
            virtual void select(const std::string& port, int baud, std::optional<bool> dtr) = 0;
            virtual void open() = 0;
            virtual void close() = 0;
            virtual void write(const std::string& bytes, uint64_t bulkId) = 0;
            virtual void acquire(const std::string& owner, int priority, int timeoutMs) = 0;
            virtual void release() = 0;
        };

        // Broker port via UsbProxy: raw bytes in and out, OS-shaped notices.
        struct ProxyBackend final : Backend {
            SerialClient& client;
            UsbProxy::Handle handle = 0;
            std::string channel;

            ProxyBackend(SerialClient& owner, const ProxyOptions& options)
                : client(owner), channel(options.logicalDevice) {
                UsbProxy::instance().setService(options.serviceScript, options.python);
                SerialClient* self = &owner;
                PortListener listener;
                listener.read = [self](const char* data, size_t size) { self->portRead(data, size); };
                listener.notice = [self](const PortNotice& notice) { self->portNotice(notice); };
                listener.writeDone = [self](uint64_t id, size_t written) { self->portWriteDone(id, written); };
                listener.exclusive = [self](bool granted, const std::string& reason) {
                    self->portExclusive(granted, reason);
                };
                handle = UsbProxy::instance().openPort(channel, std::move(listener));
            }
            ~ProxyBackend() override {
                client.portClosed();
                UsbProxy::instance().closePort(handle);
            }
            void select(const std::string& port, int baud, std::optional<bool> dtr) override {
                UsbProxy::instance().configure(handle, port, baud, dtr);
            }
            void open() override { client.portOpened("proxy:" + channel); }
            void close() override { client.portClosed(); }
            void write(const std::string& bytes, uint64_t bulkId) override {
                if (bulkId != 0) UsbProxy::instance().writeBulk(handle, bytes, bulkId);
                else UsbProxy::instance().write(handle, bytes);
            }
            void acquire(const std::string& owner, int priority, int timeoutMs) override {
                UsbProxy::instance().acquireExclusive(handle, owner, priority, timeoutMs);
            }
            void release() override { UsbProxy::instance().releaseExclusive(handle); }
        };

        // Direct Win32 port: worker I/O plus CM device-interface notifications.
        struct Win32Backend final : Backend {
            SerialClient& client;
            std::mutex mutex;
            std::condition_variable cv;
            bool signaled = true;   // first pass seeds
            bool stopping = false;
            HCMNOTIFICATION notification = nullptr;
            std::thread watcher;

            explicit Win32Backend(SerialClient& owner) : client(owner) {
                // GUID_DEVINTERFACE_COMPORT {86E0D1E0-8089-11D0-9CE4-08003E301F73}
                static constexpr GUID ComPortInterface = {0x86E0D1E0, 0x8089, 0x11D0,
                    {0x9C, 0xE4, 0x08, 0x00, 0x3E, 0x30, 0x1F, 0x73}};
                CM_NOTIFY_FILTER filter{};
                filter.cbSize = sizeof(filter);
                filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
                filter.u.DeviceInterface.ClassGuid = ComPortInterface;
                if (CM_Register_Notification(&filter, this, &Win32Backend::changed,
                        &notification) != CR_SUCCESS) {
                    notification = nullptr;
                    dbg("[SerialClient] CM_Register_Notification failed");
                }
                watcher = std::thread([this] { watch(); });
            }
            ~Win32Backend() override {
                if (notification) CM_Unregister_Notification(notification);
                {
                    std::lock_guard lock(mutex);
                    stopping = true;
                }
                cv.notify_all();
                if (watcher.joinable()) watcher.join();
                client.rawClose();
            }

            // System thread pool: only enqueue.
            static DWORD CALLBACK changed(HCMNOTIFICATION, PVOID context, CM_NOTIFY_ACTION,
                                          PCM_NOTIFY_EVENT_DATA, DWORD) {
                auto* self = static_cast<Win32Backend*>(context);
                {
                    std::lock_guard lock(self->mutex);
                    self->signaled = true;
                }
                self->cv.notify_all();
                return ERROR_SUCCESS;
            }

            void signal() {
                {
                    std::lock_guard lock(mutex);
                    signaled = true;
                }
                cv.notify_all();
            }

            // Event-driven. After each notification a bounded settle window
            // (3 s, re-read every 250 ms) absorbs DEVICEMAP lag; then idle.
            void watch() {
                using namespace std::chrono_literals;
                bool first = true, lastPresent = false;
                std::vector<PortInfo> lastPorts;
                std::string lastPort;
                std::unique_lock lock(mutex);
                while (true) {
                    cv.wait(lock, [this] { return signaled || stopping; });
                    if (stopping) return;
                    signaled = false;
                    auto until = std::chrono::steady_clock::now() + 3s;
                    while (!stopping && std::chrono::steady_clock::now() < until) {
                        lock.unlock();
                        std::vector<PortInfo> ports = enumeratePortDetails();
                        const std::string selected = client.selectedPort();
                        PortNotice state;
                        state.port = selected;
                        for (const PortInfo& info : ports)
                            if (!selected.empty() && info.port == selected) {
                                state.present = true;
                                state.vid = info.vid;
                                state.pid = info.pid;
                            }
                        if (first || ports != lastPorts) {
                            PortNotice inventory;
                            inventory.kind = PortNotice::Kind::Inventory;
                            inventory.reason = "inventory";
                            inventory.ports = ports;
                            inventory.seed = first;
                            client.portNotice(inventory);
                        }
                        if (first || state.present != lastPresent || selected != lastPort) {
                            state.seed = first;
                            client.portNotice(state);
                        }
                        first = false;
                        lastPorts = std::move(ports);
                        lastPresent = state.present;
                        lastPort = selected;
                        lock.lock();
                        if (cv.wait_for(lock, 250ms, [this] { return signaled || stopping; })) {
                            if (stopping) return;
                            signaled = false;
                            until = std::chrono::steady_clock::now() + 3s;
                        }
                    }
                }
            }

            void select(const std::string&, int, std::optional<bool>) override { signal(); }
            void open() override { client.rawOpen(); }
            void close() override { client.rawClose(); }
            void write(const std::string& bytes, uint64_t bulkId) override { client.rawWrite(bytes, bulkId); }
            // The direct route's exclusive access is CreateFile's share mode 0.
            void acquire(const std::string&, int, int) override { client.portExclusive(true, ""); }
            void release() override {}
        };

        // -- Shared state ---------------------------------------------------

        struct Tasks {
            std::recursive_mutex mutex;
            SerialClient* owner = nullptr;
            std::mutex queueMutex;
            std::vector<std::function<void()>> queue;
            std::atomic<bool> queued = false;
        };
        std::shared_ptr<Tasks> tasks_ = std::make_shared<Tasks>();

        ProxyOptions proxyOptions_;
        std::unique_ptr<Backend> backend_;
        std::vector<std::function<void(DeviceEvent&)>> deviceListeners_;
        std::vector<std::function<void(BulkDoneEvent&)>> bulkListeners_;
        std::atomic<uint64_t> nextBulk_ = 0;
        std::mutex selectionMutex_;
        std::string selectedPort_;
        std::atomic<int> selectedBaud_ = 115200;

        // Device view (promise thread only).
        bool deviceKnown_ = false, available_ = false, inventoryKnown_ = false;
        bool leased_ = false, mine_ = false;
        std::string devicePort_;
        std::vector<PortInfo> ports_;

        void ensureBackend() {
            if (!backend_) setProxy(proxyOptions_);
        }

        std::string selectedPort() {
            std::lock_guard lock(selectionMutex_);
            return selectedPort_;
        }

        // Marshal to the promise thread; dropped after destruction began.
        void post(std::function<void()> task) {
            auto tasks = tasks_;
            {
                std::lock_guard lock(tasks->queueMutex);
                tasks->queue.push_back(std::move(task));
            }
            if (tasks->queued.exchange(true)) return;
            Core::Async::Do([tasks]() -> Core::Promise<void> {
                std::lock_guard lock(tasks->mutex);
                tasks->queued = false;
                std::vector<std::function<void()>> batch;
                {
                    std::lock_guard queueLock(tasks->queueMutex);
                    batch.swap(tasks->queue);
                }
                if (!tasks->owner) co_return;
                for (auto& task : batch) task();
                co_return;
            });
        }

        // -- Backend callbacks (any thread; only enqueue) ------------------

        void portRead(const char* data, size_t size) {
            if (running && isConnected) fireData(data, static_cast<int>(size));
        }

        void portNotice(const PortNotice& notice) {
            post([this, notice] { applyNotice(notice); });
        }

        void portWriteDone(uint64_t id, size_t written) {
            post([this, id, written] {
                BulkDoneEvent event{id, written};
                for (auto& listener : bulkListeners_) listener(event);
            });
        }

        void portExclusive(bool granted, std::string reason) {
            post([this, granted, reason] {
                DeviceEvent event = deviceEvent();
                if (granted) { leased_ = true; mine_ = true; }
                event.leased = leased_;
                event.mine = mine_;
                event.reason = granted ? "lease_granted"
                                       : "lease_denied" + (reason.empty() ? "" : ": " + reason);
                emit(event);
            });
        }

        void portOpened(const std::string& address) {
            running = true;
            isConnected = true;
            fireConnect(address);
        }

        void portClosed() {
            const bool wasActive = running.exchange(false);
            isConnected = false;
            if (wasActive) fireDisconnect();
        }

        DeviceEvent deviceEvent() const {
            DeviceEvent event;
            event.port = devicePort_;
            event.ports = ports_;
            event.leased = leased_;
            event.mine = mine_;
            for (const PortInfo& info : ports_)
                if (info.port == devicePort_) { event.vid = info.vid; event.pid = info.pid; }
            return event;
        }

        void emit(DeviceEvent& event) {
            for (auto& listener : deviceListeners_) listener(event);
        }

        // Shared edge logic: Available/Unavailable only on a change of
        // usability; link loss and inventory are Changed.
        void applyNotice(const PortNotice& notice) {
            if (notice.kind == PortNotice::Kind::Inventory) {
                if (inventoryKnown_ && notice.ports == ports_ && !notice.seed) return;
                inventoryKnown_ = true;
                ports_ = notice.ports;
                DeviceEvent event = deviceEvent();
                event.inventory = true;
                event.reason = notice.reason;
                event.seed = notice.seed;
                emit(event);
                return;
            }
            if (notice.kind == PortNotice::Kind::LinkLost) {
                DeviceEvent event = deviceEvent();
                event.reason = notice.reason;
                emit(event);
                return;
            }
            const bool usable = notice.present && !notice.exclusiveElsewhere;
            const bool leased = notice.exclusiveElsewhere || notice.exclusiveMine;
            // A usable port that replaced another usable port is a new device
            // for the owner: report it as an Available edge.
            const bool edge = !deviceKnown_ || usable != available_
                || (usable && !devicePort_.empty() && notice.port != devicePort_);
            const bool different = notice.port != devicePort_ || leased != leased_
                || notice.exclusiveMine != mine_;
            if (!edge && !different && !notice.seed) return;
            deviceKnown_ = true;
            available_ = usable;
            devicePort_ = notice.port;
            leased_ = leased;
            mine_ = notice.exclusiveMine;
            DeviceEvent event = deviceEvent();
            if (notice.vid >= 0) { event.vid = notice.vid; event.pid = notice.pid; }
            event.seed = notice.seed;
            event.reason = notice.reason;
            if (edge) {
                event.state = usable ? DeviceEvent::State::Available : DeviceEvent::State::Unavailable;
                if (event.reason.empty())
                    event.reason = usable ? "available"
                        : notice.exclusiveElsewhere ? "leased" : "unavailable";
            }
            emit(event);
        }

        // -- Win32 worker state ------------------------------------------

        struct Outgoing {
            std::string data;
            uint64_t bulkId = 0;
        };

        std::string          port_;
        int                  baud_       = 115200;
        std::atomic<HANDLE>  handle_     { INVALID_HANDLE_VALUE };
        std::thread          worker_;
        std::mutex           outgoingMutex_;
        std::vector<Outgoing> outgoing_;
        std::optional<bool> dtrEnabled_;
        std::atomic<bool> stopRequested_ = false;
        std::atomic<bool> finished_ = true;
        bool pendingConnect_ = false, discardEvents_ = false;
        std::string pendingPort_;
        int pendingBaud_ = 115200;

        // Main-thread facade. Opening, configuring and closing remain worker-owned.
        void rawOpen() {
            if (running) return;
            {
                std::lock_guard<std::mutex> lock(outgoingMutex_);
                outgoing_.clear();
            }
            pendingPort_ = selectedPort();
            pendingBaud_ = selectedBaud_;
            pendingConnect_ = true;
            running = true;
            isConnected = false;
            startPump();
            auto state = delivery_;
            Core::Async::Do([state]() -> Core::Promise<void> {
                while (true) {
                    {
                        std::lock_guard lock(state->mutex);
                        auto* source = static_cast<SerialClient*>(state->owner);
                        if (!source) co_return;
                        source->service();
                    }
                    co_await Core::Async::delay(std::chrono::milliseconds(16));
                }
            });
        }

        void rawWrite(const std::string& msg, uint64_t bulkId) {
            // WriteFile must never run on Rev's main thread. Queue complete
            // messages and let the port-owning worker drain them in order.
            std::lock_guard<std::mutex> lock(outgoingMutex_);
            outgoing_.push_back({msg, bulkId});
        }

        void rawClose() {
            const bool wasActive = running.exchange(false);
            isConnected = false;
            pendingConnect_ = false;
            stopRequested_ = true;
            discardEvents_ = true;
            // Cancellation does not wait for the driver or close its handle.
            if (worker_.joinable()) CancelSynchronousIo(worker_.native_handle());
            {
                std::lock_guard<std::mutex> lock(outgoingMutex_);
                outgoing_.clear();
            }
            // Keep the pump alive until worker cleanup has completed.
            if (wasActive) {
                DisconnectEvent event{};
                disconnectDispatcher_.tell(&SerialClient::disconnectEvent, event);
            }
        }

        void service() {
            if (discardEvents_) {
                std::lock_guard<std::mutex> lock(queueMutex_);
                queue_.clear();
            }
            if (finished_) {
                // finished_ is published only after all driver operations end.
                if (worker_.joinable()) worker_.join();
                if (pendingConnect_) {
                    {
                        std::lock_guard<std::mutex> lock(queueMutex_);
                        queue_.clear();
                    }
                    port_ = std::move(pendingPort_);
                    baud_ = pendingBaud_;
                    pendingConnect_ = false;
                    discardEvents_ = false;
                    stopRequested_ = false;
                    finished_ = false;
                    worker_ = std::thread([this]() {
                        workerMain();
                        finished_ = true;
                    });
                } else {
                    running = false;
                }
            }
            if (!discardEvents_) pump();
            if (finished_ && !pendingConnect_ && !running) stopPump();
        }

        // -- Worker ------------------------------------------------------

        void workerMain() {

            std::string address = port_ + " @ " + std::to_string(baud_);
            fireConnecting(address);

            // Open \\.\COMx (the \\.\ prefix is required for COM10 and above).
            std::string path = "\\\\.\\" + port_;
            HANDLE h = CreateFileA(
                path.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,               // no sharing
                nullptr,
                OPEN_EXISTING,
                0,               // synchronous I/O
                nullptr
            );

            if (h == INVALID_HANDLE_VALUE) {
                dbg("[SerialClient] Failed to open %s (%lu)", port_.c_str(), GetLastError());
                fireError("Failed to open " + port_);
                stopRequested_ = true;
                return;
            }

            // DCB: 115200 8-N-1, NO flow control (matches the stock controller).
            DCB dcb{};
            dcb.DCBlength = sizeof(DCB);
            if (!GetCommState(h, &dcb)) {
                dbg("[SerialClient] GetCommState failed");
                fireError("GetCommState failed");
                CloseHandle(h);
                stopRequested_ = true;
                return;
            }

            dcb.BaudRate        = (DWORD)baud_;
            dcb.ByteSize        = 8;
            dcb.Parity          = NOPARITY;
            dcb.StopBits        = ONESTOPBIT;
            dcb.fBinary         = TRUE;
            dcb.fOutxCtsFlow    = FALSE;
            dcb.fOutxDsrFlow    = FALSE;
            // Preserve DTR unless this specific client requires a stable CDC
            // carrier assertion. Never pulse DTR/RTS during discovery.
            if (dtrEnabled_)
                dcb.fDtrControl = *dtrEnabled_ ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
            dcb.fDsrSensitivity = FALSE;
            dcb.fOutX           = FALSE;
            dcb.fInX            = FALSE;

            if (!SetCommState(h, &dcb)) {
                dbg("[SerialClient] SetCommState failed");
                fireError("SetCommState failed");
                CloseHandle(h);
                stopRequested_ = true;
                return;
            }

            // Byte-oriented bounded-poll reads (the documented COMMTIMEOUTS
            // idiom): ReadFile returns immediately with buffered bytes, or
            // waits up to ~50ms for the first byte. Disconnect stays prompt.
            COMMTIMEOUTS timeouts{};
            timeouts.ReadIntervalTimeout         = MAXDWORD;
            timeouts.ReadTotalTimeoutConstant    = 50;
            timeouts.ReadTotalTimeoutMultiplier  = MAXDWORD;
            timeouts.WriteTotalTimeoutConstant   = 100;
            timeouts.WriteTotalTimeoutMultiplier = 0;
            if (!SetCommTimeouts(h, &timeouts)) {
                fireError("SetCommTimeouts failed");
                CloseHandle(h);
                stopRequested_ = true;
                return;
            }

            handle_.store(h);

            PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
            if (stopRequested_) {
                isConnected = false;
                HANDLE cur = handle_.exchange(INVALID_HANDLE_VALUE);
                if (cur != INVALID_HANDLE_VALUE) { CloseHandle(cur); }
                return;
            }

            dbg("[SerialClient] Connected to %s", address.c_str());
            isConnected = true;
            fireConnect(address);

            readLoop(h);

            // Cleanup — atomic exchange: only one of worker/disconnect closes.
            isConnected = false;
            HANDLE cur = handle_.exchange(INVALID_HANDLE_VALUE);
            if (cur != INVALID_HANDLE_VALUE) { CloseHandle(cur); }
            stopRequested_ = true;
        }

        void readLoop(HANDLE h) {

            char buffer[4096];
            auto lastBeat = std::chrono::steady_clock::now();

            while (!stopRequested_) {

                // Heartbeat: pulse the status-poll byte at the configured cadence.
                const int interval = heartbeatIntervalMs_.load();
                if (interval > 0) {
                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastBeat).count() >= interval) {
                        rawWrite(heartbeatMessage_, 0);
                        lastBeat = now;
                    }
                }

                if (!drainOutgoing(h)) { break; }

                DWORD bytes = 0;
                BOOL  ok    = ReadFile(h, buffer, sizeof(buffer), &bytes, nullptr);

                if (stopRequested_) { break; }

                if (!ok) {
                    DWORD err = GetLastError();
                    dbg("[SerialClient] ReadFile failed (%lu)", err);
                    fireDisconnect();
                    break;
                }

                if (bytes > 0) {
                    fireData(buffer, (int)bytes);
                }
            }
        }

        bool drainOutgoing(HANDLE h) {

            std::vector<Outgoing> batch;
            {
                std::lock_guard<std::mutex> lock(outgoingMutex_);
                // Bound each write batch so incoming replies are serviced under load.
                std::size_t count = 0, bytes = 0;
                while (count < outgoing_.size() && bytes < 256U) {
                    bytes += outgoing_[count].data.size();
                    batch.push_back(std::move(outgoing_[count++]));
                }
                outgoing_.erase(outgoing_.begin(), outgoing_.begin() + count);
            }

            for (const Outgoing& message : batch) {
                std::size_t sent = 0;

                while (sent < message.data.size() && !stopRequested_) {
                    DWORD written = 0;
                    const DWORD remaining = static_cast<DWORD>(message.data.size() - sent);
                    const BOOL ok = WriteFile(
                        h,
                        message.data.data() + sent,
                        remaining,
                        &written,
                        nullptr
                    );

                    if (!ok || written == 0) {
                        const DWORD error = GetLastError();
                        dbg(
                            "[SerialClient] Write failed after %zu/%zu bytes (%lu)",
                            sent,
                            message.data.size(),
                            error
                        );
                        if (!stopRequested_) {
                            isConnected = false;
                            fireError("Serial write failed (" + std::to_string(error) + ")");
                            fireDisconnect();
                        }
                        stopRequested_ = true;
                        return false;
                    }

                    sent += static_cast<std::size_t>(written);
                }
                // Direct-route bulk completion: every byte reached WriteFile.
                if (message.bulkId != 0 && sent == message.data.size())
                    portWriteDone(message.bulkId, sent);
            }

            return !stopRequested_;
        }
    };
}
