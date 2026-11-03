module;

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <windows.h>

#include <dbg.hpp>

export module Rev.UsbProxy;

import Rev.Core.Promise;
import Rev.OS.File.Json;
import Rev.Transport;
import Rev.WebSocketClient;

export namespace Rev {

    // ------------------------------------------------------------------
    // OS-shaped port vocabulary (Thesis clause 7). A port provider - Win32
    // or the broker behind UsbProxy - reports the same facts in the same
    // shape. Nothing here mentions sessions, hex, credits or leases.
    // ------------------------------------------------------------------

    struct PortInfo {
        std::string port;
        int vid = -1;
        int pid = -1;
        std::string serial;
        bool operator==(const PortInfo&) const = default;
    };

    struct PortNotice {
        enum class Kind { State, Inventory, LinkLost };
        Kind kind = Kind::State;
        // State: the selected port exists and can be opened.
        bool present = false;
        // Exclusive access is held by another client (D-2) / by this port.
        bool exclusiveElsewhere = false;
        bool exclusiveMine = false;
        std::string port;
        int vid = -1;
        int pid = -1;
        // Inventory: every serial port the provider currently sees.
        std::vector<PortInfo> ports;
        std::string reason;
        // First report after the port was opened (or re-opened by the provider).
        bool seed = false;
    };

    // Callbacks run on Rev's promise thread or on a provider thread. They
    // must only record/enqueue; never call back into the provider.
    struct PortListener {
        std::function<void(const char*, size_t)> read;
        std::function<void(const PortNotice&)> notice;
        std::function<void(uint64_t id, size_t written)> writeDone;
        std::function<void(bool granted, const std::string& reason)> exclusive;
    };

    // ------------------------------------------------------------------
    // UsbProxy - Rev-internal broker port provider (protocol 8).
    //
    // Owns the sole WebSocket to the broker, a session registry keyed by an
    // internal sid, a worker-owned send queue (per-sid round-robin, credit
    // window), and the single reconnect/ensureService authority. Callers see
    // only an opaque handle with open/configure/write/exclusive/close.
    // Application code must not use this type; use Rev::SerialClient.
    // ------------------------------------------------------------------

    struct UsbProxy final {
        using Json = OS::Json;
        using Handle = uint64_t;

        static constexpr int Protocol = 8;
        static constexpr size_t CreditWindowBytes = 256 * 1024;   // D-5
        static constexpr size_t ChunkBytes = 16 * 1024;           // D-5, decoded

        static UsbProxy& instance() {
            // Process-lifetime manager: pending reconnect work may still refer
            // to it during static teardown, so intentionally never destroy it.
            static UsbProxy* value = new UsbProxy();
            return *value;
        }

        UsbProxy(const UsbProxy&) = delete;
        UsbProxy& operator=(const UsbProxy&) = delete;

        // Register the broker's idempotent service manager. Recovery calls it
        // whenever the WebSocket cannot be reached.
        void setService(const std::string& script, const std::string& python = "python") {
            if (script.empty()) return;
            std::lock_guard lock(mutex);
            serviceScript = script;
            servicePython = python;
        }

        // Open a port on a broker channel. Device notices (including the
        // provider-wide inventory) start flowing immediately; reads are
        // delivered from the moment the broker forwards them.
        Handle openPort(const std::string& channel, PortListener callbacks) {
            const Handle handle = ++nextHandle;
            auto listener = std::make_shared<Listener>();
            listener->callbacks = std::move(callbacks);
            bool deliverInventory = false;
            PortNotice inventoryNotice;
            {
                std::lock_guard lock(mutex);
                Record record;
                record.listener = listener;
                record.channel = channel;
                auto [it, inserted] = sessions.emplace(sidOf(handle), std::move(record));
                if (linkReady) queueOpen(it->first, it->second);
                if (hasInventory) {
                    deliverInventory = true;
                    inventoryNotice = inventoryOf(inventory);
                }
            }
            startSender();
            connect();
            if (deliverInventory) deliver(listener, [&](PortListener& l) {
                if (l.notice) l.notice(inventoryNotice);
            });
            return handle;
        }

        void closePort(Handle handle) {
            std::shared_ptr<Listener> listener;
            {
                std::lock_guard lock(mutex);
                auto found = sessions.find(sidOf(handle));
                if (found == sessions.end()) return;
                listener = found->second.listener;
                if (linkReady) {
                    Json request{{"id", ++nextId}, {"command", "close"}, {"sid", found->first}};
                    control.push_back(request.dump());
                }
                sessions.erase(found);
            }
            wake.notify_all();
            std::lock_guard lock(listener->mutex);
            listener->alive = false;
        }

        // Select the physical port behind the channel (persisted by the broker).
        void configure(Handle handle, const std::string& port, int baud,
                       std::optional<bool> dtr) {
            {
                std::lock_guard lock(mutex);
                auto found = sessions.find(sidOf(handle));
                if (found == sessions.end()) return;
                Record& record = found->second;
                record.port = port;
                if (baud > 0) record.baud = baud;
                if (dtr) record.dtr = dtr;
                if (!linkReady) return;   // the next open carries it
                const uint64_t id = ++nextId;
                Json request{{"id", id}, {"command", "configure"}, {"sid", found->first}};
                request["port"] = port.empty() ? Json(nullptr) : Json(port);
                if (record.baud > 0) request["baud"] = record.baud;
                if (record.dtr) request["dtr"] = *record.dtr;
                record.requests[id] = "configure";
                record.queue.push_back({request.dump(), 0, 0});
            }
            wake.notify_all();
        }

        // Plain write; chunked, hex-encoded and credited internally. Dropped
        // while the provider link is down (like a write to a vanished port).
        void write(Handle handle, const std::string& bytes) { enqueueWrite(handle, bytes, 0); }

        // Bulk write: one logical transfer, completion via writeDone(id).
        void writeBulk(Handle handle, const std::string& bytes, uint64_t id) {
            enqueueWrite(handle, bytes, id == 0 ? 1 : id);
        }

        // Exclusive access. Replayed automatically after a reconnect (D-3).
        void acquireExclusive(Handle handle, const std::string& owner, int priority,
                              int timeoutMs) {
            {
                std::lock_guard lock(mutex);
                auto found = sessions.find(sidOf(handle));
                if (found == sessions.end()) return;
                Record& record = found->second;
                record.leaseWanted = true;
                record.leaseOwner = owner;
                record.leasePriority = priority;
                record.leaseTimeout = timeoutMs;
                if (linkReady) queueLease(found->first, record);
            }
            wake.notify_all();
        }

        void releaseExclusive(Handle handle) {
            {
                std::lock_guard lock(mutex);
                auto found = sessions.find(sidOf(handle));
                if (found == sessions.end()) return;
                Record& record = found->second;
                record.leaseWanted = false;
                if (!linkReady) return;
                const uint64_t id = ++nextId;
                Json request{{"id", id}, {"command", "release"}, {"sid", found->first}};
                record.requests[id] = "release";
                record.queue.push_back({request.dump(), 0, 0});
            }
            wake.notify_all();
        }

    private:
        struct Listener {
            std::recursive_mutex mutex;
            PortListener callbacks;
            bool alive = true;
        };
        struct Item {
            std::string text;
            size_t bytes = 0;     // decoded payload bytes (credit), 0 = control
            uint64_t seq = 0;     // write sequence, 0 = control
        };
        struct Record {
            std::shared_ptr<Listener> listener;
            std::string channel;
            std::string port;
            int baud = 0;
            std::optional<bool> dtr;
            bool leaseWanted = false;
            std::string leaseOwner;
            int leasePriority = 0;
            int leaseTimeout = 0;
            std::deque<Item> queue;
            size_t inflight = 0;
            std::map<uint64_t, size_t> unacked;
            uint64_t nextSeq = 0;
            std::map<uint64_t, std::string> requests;
        };

        WebSocketClient socket;
        std::mutex mutex;
        std::condition_variable wake;
        std::map<std::string, Record> sessions;
        std::deque<std::string> control;
        std::string cursor;
        bool linkReady = false;
        bool stale = false;
        bool hasInventory = false;
        Json inventory;
        int64_t brokerPid = -1;
        std::atomic<uint64_t> nextId = 0;          // L3: atomic request ids
        std::atomic<uint64_t> nextHandle = 0;
        std::once_flag senderStarted;
        std::string serviceScript;
        std::string servicePython = "python";
        std::mutex serviceMutex;
        HANDLE serviceProcess = nullptr;
        std::atomic<bool> recovering = false;

        UsbProxy() {
            socket.onConnect([this](Transport::ConnectEvent&) { linkUp(); });
            socket.onData([this](Transport::DataEvent& data) { received(data); });
            socket.onError([this](Transport::ErrorEvent& error) { serviceLost(error.reason); });
            socket.onDisconnect([this](Transport::DisconnectEvent&) {
                serviceLost("USB proxy WebSocket disconnected");
            });
        }

        static std::string sidOf(Handle handle) {
            return "rev-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(handle);
        }

        void connect() {
            if (socket.running) return;
            socket.connect("127.0.0.1", 8750);
            startRecovery();
        }

        // -- Link lifecycle ------------------------------------------------

        // Caller holds mutex.
        void queueOpen(const std::string& sid, Record& record) {
            const uint64_t id = ++nextId;
            Json request{{"id", id}, {"command", "open"}, {"sid", sid},
                         {"channel", record.channel}, {"inventory", true}};
            if (!record.port.empty()) request["port"] = record.port;
            if (record.baud > 0) request["baud"] = record.baud;
            if (record.dtr) request["dtr"] = *record.dtr;
            record.requests[id] = "open";
            record.queue.push_back({request.dump(), 0, 0});
        }

        // Caller holds mutex.
        void queueLease(const std::string& sid, Record& record) {
            const uint64_t id = ++nextId;
            Json request{{"id", id}, {"command", "lease"}, {"sid", sid},
                         {"owner", record.leaseOwner}, {"priority", record.leasePriority},
                         {"timeout", record.leaseTimeout}};
            record.requests[id] = "lease";
            record.queue.push_back({request.dump(), 0, 0});
        }

        // On (re)connect: one open per live sid, then auto re-lease (D-3).
        void linkUp() {
            {
                std::lock_guard lock(mutex);
                control.clear();
                hasInventory = false;
                for (auto& [sid, record] : sessions) {
                    record.queue.clear();
                    record.inflight = 0;
                    record.unacked.clear();
                    record.requests.clear();
                    queueOpen(sid, record);
                    if (record.leaseWanted) queueLease(sid, record);
                }
                linkReady = true;
                stale = false;
            }
            wake.notify_all();
        }

        // Losing the WebSocket means "state unknown", never "device lost".
        // Ports stay open; each is told link_lost exactly once per loss.
        // Runs re-entrantly inside WebSocketClient callbacks: only bookkeeping.
        void serviceLost(const std::string& reason) {
            std::vector<std::shared_ptr<Listener>> listeners;
            {
                std::lock_guard lock(mutex);
                if (linkReady) {
                    linkReady = false;
                    stale = socket.isConnected;
                    control.clear();
                    for (auto& [sid, record] : sessions) {
                        record.queue.clear();
                        record.inflight = 0;
                        record.unacked.clear();
                        record.requests.clear();
                        listeners.push_back(record.listener);
                    }
                }
            }
            if (!listeners.empty()) dbg("[UsbProxy] link lost: %s", reason.c_str());
            PortNotice notice;
            notice.kind = PortNotice::Kind::LinkLost;
            notice.reason = "link_lost";
            for (auto& listener : listeners)
                deliver(listener, [&](PortListener& l) { if (l.notice) l.notice(notice); });
            startRecovery();
        }

        // The single client-side reconnect loop (contract open point:
        // "reconnect the control link only"). Single-flight; bounded backoff
        // 0.5 s -> 5 s; (re)starts the broker through the idempotent service
        // manager and stops as soon as the WebSocket is connected.
        void startRecovery() {
            if (recovering.exchange(true)) return;
            auto self = this;
            Core::Async::Do([self]() -> Core::Promise<void> {
                using namespace std::chrono_literals;
                std::chrono::milliseconds backoff = 500ms;
                while (true) {
                    co_await Core::Async::delay(backoff);
                    bool dropStale = false;
                    {
                        std::lock_guard lock(self->mutex);
                        dropStale = self->stale;
                        self->stale = false;
                    }
                    // A send failure while the socket still looks connected:
                    // close it here (never inside its own callback).
                    if (dropStale && self->socket.isConnected) self->socket.disconnect();
                    if (self->socket.isConnected) break;
                    if (!self->socket.running) {
                        std::string script, python;
                        {
                            std::lock_guard lock(self->mutex);
                            script = self->serviceScript;
                            python = self->servicePython;
                        }
                        if (!script.empty()) self->ensureService(script, python);
                        self->socket.connect("127.0.0.1", 8750);
                    }
                    backoff = std::min<std::chrono::milliseconds>(backoff * 2, 5000ms);
                }
                self->recovering = false;
                // A loss that raced the exit above must not be stranded.
                if (!self->socket.isConnected) self->startRecovery();
            });
        }

        // Start the idempotent service manager. L1: never launch a second
        // manager while the previous one is still running.
        bool ensureService(const std::string& script, const std::string& python) {
            std::lock_guard lock(serviceMutex);
            if (serviceProcess) {
                if (WaitForSingleObject(serviceProcess, 0) == WAIT_TIMEOUT) return true;
                CloseHandle(serviceProcess);
                serviceProcess = nullptr;
            }
            std::wstring command = wide(python + " \"" + script + "\" start");
            STARTUPINFOW startup{};
            startup.cb = sizeof(STARTUPINFOW);
            PROCESS_INFORMATION process{};
            if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                    CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return false;
            CloseHandle(process.hThread);
            serviceProcess = process.hProcess;
            return true;
        }

        // -- Send queue ------------------------------------------------------

        void enqueueWrite(Handle handle, const std::string& bytes, uint64_t bulkId) {
            {
                std::lock_guard lock(mutex);
                auto found = sessions.find(sidOf(handle));
                if (found == sessions.end()) return;
                if (!linkReady) {
                    dbg("[UsbProxy] write dropped (link down): %zu bytes", bytes.size());
                    return;
                }
                Record& record = found->second;
                size_t offset = 0;
                do {
                    const size_t count = std::min(ChunkBytes, bytes.size() - offset);
                    const uint64_t seq = ++record.nextSeq;
                    Json request{{"command", "write"}, {"sid", found->first}, {"seq", seq},
                                 {"data", hex(bytes.data() + offset, count)}};
                    if (bulkId != 0 && offset == 0)
                        request["bulk"] = Json{{"id", bulkId}, {"total", bytes.size()},
                                               {"suppress_read", true}};
                    record.queue.push_back({request.dump(), count, seq});
                    offset += count;
                } while (offset < bytes.size());
            }
            wake.notify_all();
        }

        void startSender() {
            std::call_once(senderStarted, [this] {
                std::thread([this] { senderMain(); }).detach();
            });
        }

        // Worker-owned: WinHTTP sends never run on Rev's main thread.
        void senderMain() {
            std::unique_lock lock(mutex);
            while (true) {
                std::string text;
                if (!linkReady || !takeNext(text)) { wake.wait(lock); continue; }
                lock.unlock();
                socket.send(text);
                lock.lock();
            }
        }

        // Caller holds mutex. Session-less control first, then one item per
        // sid in round-robin order; writes wait for credit.
        bool takeNext(std::string& text) {
            if (!control.empty()) {
                text = std::move(control.front());
                control.pop_front();
                return true;
            }
            if (sessions.empty()) return false;
            auto start = sessions.upper_bound(cursor);
            for (size_t visited = 0; visited < sessions.size(); ++visited) {
                if (start == sessions.end()) start = sessions.begin();
                Record& record = start->second;
                if (!record.queue.empty()) {
                    Item& head = record.queue.front();
                    const bool credited = head.seq == 0 || record.inflight == 0
                        || record.inflight + head.bytes <= CreditWindowBytes;
                    if (credited) {
                        if (head.seq != 0) {
                            record.inflight += head.bytes;
                            record.unacked[head.seq] = head.bytes;
                        }
                        text = std::move(head.text);
                        record.queue.pop_front();
                        cursor = start->first;
                        return true;
                    }
                }
                ++start;
            }
            return false;
        }

        // -- Receive ---------------------------------------------------------

        template<typename Call>
        static void deliver(const std::shared_ptr<Listener>& listener, Call&& call) {
            if (!listener) return;
            std::lock_guard lock(listener->mutex);
            if (listener->alive) call(listener->callbacks);
        }

        void received(Transport::DataEvent& data) {
            Json message = Json::parse(
                std::string(data.data.data(), data.data.size()), nullptr, false);
            if (message.is_discarded() || !message.is_object()) return;
            // Route on sid first. A message without sid is a protocol error.
            if (!message.contains("sid") || !message["sid"].is_string()) {
                dbg("[UsbProxy] protocol error: message without sid");
                return;
            }
            const std::string sid = message["sid"].get<std::string>();
            const std::string event = message.value("event", "");
            if (sid == "*") { receivedShared(event, message); return; }

            std::shared_ptr<Listener> listener;
            std::string command;
            bool credited = false;
            {
                std::lock_guard lock(mutex);
                auto found = sessions.find(sid);
                if (found == sessions.end()) return;   // closed locally
                Record& record = found->second;
                listener = record.listener;
                if (message.contains("id") && message["id"].is_number_unsigned()) {
                    auto request = record.requests.find(message["id"].get<uint64_t>());
                    if (request != record.requests.end()) {
                        command = request->second;
                        record.requests.erase(request);
                    }
                    if (command == "lease" && !message.value("ok", false))
                        record.leaseWanted = false;
                }
                else if (event == "ack" && message.contains("seq")
                         && message["seq"].is_number_unsigned()) {
                    auto unacked = record.unacked.find(message["seq"].get<uint64_t>());
                    if (unacked != record.unacked.end()) {
                        record.inflight -= std::min(record.inflight, unacked->second);
                        record.unacked.erase(unacked);
                        credited = true;
                    }
                }
            }
            if (credited) { wake.notify_all(); return; }

            if (message.contains("id")) {
                const bool ok = message.value("ok", false);
                if (command == "open" || command == "configure") {
                    if (!ok) dbg("[UsbProxy] %s failed: %s", command.c_str(), message.dump().c_str());
                    if (ok && message.contains("device") && message["device"].is_object()) {
                        PortNotice notice = stateOf(message["device"], "",
                                                    command == "open" || message.value("seed", false));
                        deliver(listener, [&](PortListener& l) { if (l.notice) l.notice(notice); });
                    }
                }
                else if (command == "lease") {
                    const std::string reason = ok ? "" : message.value("code",
                        message.value("message", std::string("denied")));
                    deliver(listener, [&](PortListener& l) { if (l.exclusive) l.exclusive(ok, reason); });
                }
                return;
            }
            if (event == "data") {
                if (!message.contains("data") || !message["data"].is_string()) return;
                std::string bytes;
                if (!unhex(message["data"].get_ref<const std::string&>(), bytes)) {
                    dbg("[UsbProxy] bad hex from broker");
                    return;
                }
                deliver(listener, [&](PortListener& l) {
                    if (l.read && !bytes.empty()) l.read(bytes.data(), bytes.size());
                });
            }
            else if (event == "device") {
                if (!message.contains("device") || !message["device"].is_object()) return;
                PortNotice notice = stateOf(message["device"], message.value("reason", ""),
                                            message.value("seed", false));
                deliver(listener, [&](PortListener& l) { if (l.notice) l.notice(notice); });
            }
            else if (event == "bulk_done") {
                const uint64_t id = message.value("id", uint64_t(0));
                const size_t written = message.value("written", size_t(0));
                deliver(listener, [&](PortListener& l) { if (l.writeDone) l.writeDone(id, written); });
            }
            else if (event == "error") {
                dbg("[UsbProxy] broker error (%s): %s", sid.c_str(), message.dump().c_str());
            }
        }

        // "*" messages: hello is the link's own fact; inventory fans out to
        // every port (all are opened with inventory:true).
        void receivedShared(const std::string& event, Json& message) {
            if (event == "hello") {
                const int64_t pid = message.contains("pid") && message["pid"].is_number_integer()
                    ? message["pid"].get<int64_t>() : -1;
                std::lock_guard lock(mutex);
                brokerPid = pid;
                return;
            }
            if (event == "inventory") {
                std::vector<std::shared_ptr<Listener>> listeners;
                PortNotice notice = inventoryOf(message);
                {
                    std::lock_guard lock(mutex);
                    inventory = message;
                    hasInventory = true;
                    for (auto& [sid, record] : sessions) listeners.push_back(record.listener);
                }
                for (auto& listener : listeners)
                    deliver(listener, [&](PortListener& l) { if (l.notice) l.notice(notice); });
                return;
            }
            if (event == "error") dbg("[UsbProxy] broker error: %s", message.dump().c_str());
        }

        static int usbId(const Json& entry, const char* key) {
            if (!entry.contains(key)) return -1;
            const Json& value = entry[key];
            if (value.is_number_integer()) return static_cast<int>(value.get<int64_t>());
            if (!value.is_string()) return -1;
            try { return static_cast<int>(std::stol(value.get<std::string>(), nullptr, 16)); }
            catch (...) { return -1; }
        }

        static std::string text(const Json& entry, const char* key) {
            return entry.contains(key) && entry[key].is_string()
                ? entry[key].get<std::string>() : std::string();
        }

        static PortNotice stateOf(const Json& device, const std::string& reason, bool seed) {
            PortNotice notice;
            notice.kind = PortNotice::Kind::State;
            const bool leased = device.value("leased", false);
            const bool mine = device.value("mine", false);
            notice.present = text(device, "lifecycle") == "ready";
            notice.exclusiveElsewhere = leased && !mine;
            notice.exclusiveMine = leased && mine;
            notice.port = text(device, "selected_port");
            if (notice.port.empty()) notice.port = text(device, "port");
            notice.vid = usbId(device, "vid");
            notice.pid = usbId(device, "pid");
            notice.reason = reason;
            notice.seed = seed;
            return notice;
        }

        static PortNotice inventoryOf(const Json& message) {
            PortNotice notice;
            notice.kind = PortNotice::Kind::Inventory;
            notice.reason = "inventory";
            if (!message.contains("ports") || !message["ports"].is_array()) return notice;
            for (const Json& entry : message["ports"]) {
                if (!entry.is_object()) continue;
                PortInfo info;
                info.port = text(entry, "port");
                if (info.port.empty()) info.port = text(entry, "device");
                if (info.port.empty()) continue;
                info.vid = usbId(entry, "vid");
                info.pid = usbId(entry, "pid");
                info.serial = text(entry, "serial");
                notice.ports.push_back(std::move(info));
            }
            return notice;
        }

        // -- Hex (transport encoding only; never inspected) ------------------

        static std::string hex(const char* data, size_t size) {
            static constexpr char digits[] = "0123456789abcdef";
            std::string result(size * 2, '0');
            for (size_t index = 0; index < size; ++index) {
                const auto byte = static_cast<uint8_t>(data[index]);
                result[index * 2] = digits[byte >> 4];
                result[index * 2 + 1] = digits[byte & 15];
            }
            return result;
        }

        static bool unhex(const std::string& text, std::string& out) {
            static const auto table = [] {
                std::array<int8_t, 256> values{};
                values.fill(-1);
                for (int i = 0; i < 10; ++i) values['0' + i] = static_cast<int8_t>(i);
                for (int i = 0; i < 6; ++i) {
                    values['a' + i] = static_cast<int8_t>(10 + i);
                    values['A' + i] = static_cast<int8_t>(10 + i);
                }
                return values;
            }();
            if (text.size() % 2 != 0) return false;
            out.resize(text.size() / 2);
            for (size_t index = 0; index < out.size(); ++index) {
                const int high = table[static_cast<uint8_t>(text[index * 2])];
                const int low = table[static_cast<uint8_t>(text[index * 2 + 1])];
                if (high < 0 || low < 0) return false;
                out[index] = static_cast<char>((high << 4) | low);
            }
            return true;
        }

        static std::wstring wide(const std::string& value) {
            int count = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                            static_cast<int>(value.size()), nullptr, 0);
            std::wstring result(count, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                static_cast<int>(value.size()), result.data(), count);
            return result;
        }
    };
}
