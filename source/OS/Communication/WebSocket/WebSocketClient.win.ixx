module;

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>
#include <winhttp.h>

export module Rev.WebSocketClient;

import Rev.Transport;

export namespace Rev {

    // Text-message WebSocket transport. Worker events use Transport's normal
    // Promise-thread delivery path; one DataEvent is one complete WS message.
    struct WebSocketClient final : public Transport {
        std::string path = "/";

        WebSocketClient() = default;
        WebSocketClient(const WebSocketClient&) = delete;
        WebSocketClient& operator=(const WebSocketClient&) = delete;
        ~WebSocketClient() override { disconnect(); }

        void setPath(std::string value) {
            path = value.empty() ? "/" : std::move(value);
            if (path.front() != '/') path.insert(path.begin(), '/');
        }

        void connect(std::string host, int port) override {
            if (running) return;
            if (worker.joinable()) worker.join();
            host_ = std::move(host);
            port_ = port;
            running = true;
            isConnected = false;
            startPump();
            worker = std::thread([this] { workerMain(); });
        }

        void send(const std::string& message) override {
            HINTERNET socket = websocket.load();
            if (!socket || !isConnected) {
                fireError("WebSocket send failed: connection is closed");
                return;
            }
            std::lock_guard lock(sendMutex);
            DWORD error = WinHttpWebSocketSend(
                socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                const_cast<char*>(message.data()), static_cast<DWORD>(message.size()));
            if (error != NO_ERROR)
                fireError("WebSocket send failed (" + std::to_string(error) + ")");
        }

        void disconnect() override {
            bool active = running.exchange(false);
            isConnected = false;
            HINTERNET socket = websocket.exchange(nullptr);
            if (socket) {
                WinHttpWebSocketClose(socket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,
                                      nullptr, 0);
                WinHttpCloseHandle(socket);
            }
            if (worker.joinable() && std::this_thread::get_id() != worker.get_id())
                worker.join();
            if (active) fireDisconnect();
            pump();
            stopPump();
        }

    private:
        std::string host_;
        int port_ = 0;
        std::thread worker;
        std::atomic<HINTERNET> websocket{nullptr};
        std::mutex sendMutex;

        static std::wstring wide(const std::string& value) {
            if (value.empty()) return {};
            int count = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                            static_cast<int>(value.size()), nullptr, 0);
            std::wstring result(count, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                static_cast<int>(value.size()), result.data(), count);
            return result;
        }

        void workerMain() {
            std::wstring host = wide(host_);
            std::wstring requestPath = wide(path);
            std::string address = "ws://" + host_ + ":" + std::to_string(port_) + path;
            fireConnecting(address);

            HINTERNET session = WinHttpOpen(L"Rev WebSocket/1.0",
                WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS, 0);
            HINTERNET connection = session
                ? WinHttpConnect(session, host.c_str(), static_cast<INTERNET_PORT>(port_), 0)
                : nullptr;
            HINTERNET request = connection
                ? WinHttpOpenRequest(connection, L"GET", requestPath.c_str(), nullptr,
                    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0)
                : nullptr;
            bool upgraded = request
                && WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)
                && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
                && WinHttpReceiveResponse(request, nullptr);
            HINTERNET socket = upgraded ? WinHttpWebSocketCompleteUpgrade(request, 0) : nullptr;
            if (request) WinHttpCloseHandle(request);

            if (!socket) {
                fireError("WebSocket connection failed (" + std::to_string(GetLastError()) + ")");
                running = false;
            }
            else if (!running) {
                WinHttpCloseHandle(socket);
            }
            else {
                websocket = socket;
                isConnected = true;
                fireConnect(address);
                receiveLoop(socket);
            }

            isConnected = false;
            running = false;
            HINTERNET owned = websocket.exchange(nullptr);
            if (owned) WinHttpCloseHandle(owned);
            if (connection) WinHttpCloseHandle(connection);
            if (session) WinHttpCloseHandle(session);
        }

        void receiveLoop(HINTERNET socket) {
            std::vector<char> message;
            std::vector<char> buffer(16 * 1024);
            while (running) {
                DWORD read = 0;
                WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
                DWORD error = WinHttpWebSocketReceive(
                    socket, buffer.data(), static_cast<DWORD>(buffer.size()), &read, &type);
                if (error != NO_ERROR) {
                    if (running) fireError("WebSocket receive failed (" + std::to_string(error) + ")");
                    break;
                }
                if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                    if (running) fireDisconnect();
                    break;
                }
                if (type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE
                    || type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                    message.insert(message.end(), buffer.begin(), buffer.begin() + read);
                    if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                        fireData(message.data(), static_cast<int>(message.size()));
                        message.clear();
                    }
                }
            }
        }
    };
}
