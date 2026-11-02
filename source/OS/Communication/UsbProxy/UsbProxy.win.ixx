module;

#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

export module Rev.UsbProxy;

import Rev.Core.Promise;
import Rev.OS.File.Json;
import Rev.WebSocketClient;

export namespace Rev {

    struct UsbProxy final {
        using Json = OS::Json;

        struct DeviceEvent {
            std::string device;
            std::string event;
            bool connected = false;
            bool leased = false;
            Json message;
        };

        using Callback = std::function<void(DeviceEvent&)>;

        static UsbProxy& instance() {
            // Process-lifetime manager: pending reconnect work may still refer
            // to it during static teardown, so intentionally never destroy it.
            static UsbProxy* value = new UsbProxy();
            return *value;
        }

        UsbProxy(const UsbProxy&) = delete;
        UsbProxy& operator=(const UsbProxy&) = delete;

        void subscribe(void* owner, std::string device, Callback callback) {
            {
                std::lock_guard lock(mutex);
                subscriptions[owner] = {std::move(device), std::move(callback)};
            }
            connect();
            if (socket.isConnected) sendSubscriptions();
        }

        void unsubscribe(void* owner) {
            std::lock_guard lock(mutex);
            subscriptions.erase(owner);
        }

        void query(const std::string& device, int timeoutMs = 0) {
            Json request{{"id", ++nextId}, {"command", "query"},
                         {"channel", device}, {"timeout", timeoutMs}};
            socket.send(request.dump());
        }

        void selectPort(const std::string& device, const std::string& port) {
            configurePort(device, port, 0, false, false);
        }

        void configurePort(const std::string& channel, const std::string& port,
                           int baud, bool dtr) {
            configurePort(channel, port, baud, dtr, true);
        }

        void configurePort(const std::string& channel, const std::string& port,
                           int baud, bool dtr, bool settings) {
            {
                std::lock_guard lock(mutex);
                pendingSelections[channel] = {port, baud, dtr, settings};
            }
            connect();
            if (socket.isConnected) sendSelections();
        }

        void connect() {
            if (!socket.running) socket.connect("127.0.0.1", 8750);
        }

        // Start the repository's idempotent service manager without creating
        // any per-device process in the application.  This is deliberately a
        // fire-and-forget bootstrap: callers run on Rev's promise executor and
        // must never wait for process startup.  Data/control clients retry
        // independently while the manager makes the service ready.
        static bool ensureService(const std::string& script,
                                  const std::string& python = "python") {
            std::wstring command = wide(python + " \"" + script + "\" start");
            STARTUPINFOW startup{};
            startup.cb = sizeof(STARTUPINFOW);
            PROCESS_INFORMATION process{};
            if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                    CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return false;
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return true;
        }

    private:
        struct Subscription { std::string device; Callback callback; };
        WebSocketClient socket;
        std::mutex mutex;
        std::map<void*, Subscription> subscriptions;
        struct Selection {
            std::string port;
            int baud = 0;
            bool dtr = false;
            bool settings = false;
        };
        std::map<std::string, Selection> pendingSelections;
        uint64_t nextId = 0;

        UsbProxy() {
            socket.onConnect([this](Transport::ConnectEvent&) { sendSubscriptions(); });
            socket.onData([this](Transport::DataEvent& data) { received(data); });
            socket.onError([this](Transport::ErrorEvent& error) {
                serviceLost(error.reason);
            });
            socket.onDisconnect([this](Transport::DisconnectEvent&) {
                serviceLost("USB proxy WebSocket disconnected");
            });
            auto self = this;
            Core::Async::Do([self]() -> Core::Promise<void> {
                using namespace std::chrono_literals;
                while (true) {
                    if (!self->socket.running) self->connect();
                    co_await Core::Async::delay(1000ms);
                }
            });
        }

        void sendSubscriptions() {
            std::set<std::string> devices;
            {
                std::lock_guard lock(mutex);
                for (auto& [owner, subscription] : subscriptions)
                    devices.insert(subscription.device);
            }
            for (const std::string& device : devices) {
                Json request{{"id", ++nextId}, {"command", "subscribe"},
                             {"channel", device}};
                socket.send(request.dump());
            }
            sendSelections();
        }

        void sendSelections() {
            std::map<std::string, Selection> selections;
            {
                std::lock_guard lock(mutex);
                selections.swap(pendingSelections);
            }
            for (auto& [channel, selection] : selections) {
                Json request{{"id", ++nextId}, {"command", "configure"},
                             {"channel", channel}, {"port", selection.port}};
                if (selection.settings) {
                    request["baud"] = selection.baud;
                    request["dtr"] = selection.dtr;
                }
                socket.send(request.dump());
            }
        }

        void received(Transport::DataEvent& data) {
            Json message = Json::parse(
                std::string(data.data.data(), data.data.size()), nullptr, false);
            if (message.is_discarded() || !message.is_object()) return;
            if (message.contains("matches") && message["matches"].is_array()) {
                for (const Json& match : message["matches"])
                    if (match.is_object()) dispatch(match);
            }
            if (message.contains("event")) dispatch(message);
        }

        void dispatch(Json message) {
            DeviceEvent event{
                .device = message.value("channel", message.value("device", "")),
                .event = message.value("event", ""),
                .connected = message.value("connected", false),
                .leased = message.value("leased", false),
                .message = std::move(message),
            };
            std::vector<Callback> callbacks;
            {
                std::lock_guard lock(mutex);
                for (auto& [owner, subscription] : subscriptions)
                    if (subscription.device == "*" || subscription.device == event.device)
                        callbacks.push_back(subscription.callback);
            }
            for (Callback& callback : callbacks) callback(event);
        }

        void serviceLost(const std::string& reason) {
            std::vector<std::pair<std::string, Callback>> callbacks;
            {
                std::lock_guard lock(mutex);
                for (auto& [owner, subscription] : subscriptions)
                    callbacks.push_back({subscription.device, subscription.callback});
            }
            for (auto& [device, callback] : callbacks) {
                Json message{{"event", "device_unavailable"}, {"device", device},
                             {"connected", false}, {"leased", false},
                             {"error", reason}};
                DeviceEvent event{device, "device_unavailable", false, false,
                                  std::move(message)};
                callback(event);
            }
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
