module;

#include <string>
#include <vector>
#include <thread>
#include <functional>
#include <atomic>
#include <deque>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <dbg.hpp>

export module Rev.SocketClient;

export namespace Rev {

    struct SocketClient {

        struct Event {
            enum Type { Connected, Disconnected, Line, Error } type;
            std::string data;
        };

        using Callback = std::function<void(Event)>;

        SOCKET            sock     = INVALID_SOCKET;
        Callback          callback;
        std::atomic<bool> running  { false };
        std::thread       worker;

        // Create without connecting
        SocketClient(Callback cb) : callback(cb) {}

        ~SocketClient() {
            disconnect();
        }

        bool isConnected() const {
            return sock != INVALID_SOCKET;
        }

        // Attempt a TCP connection to host:port (blocking, call from non-UI thread)
        bool connect(const std::string& host, int port) {

            dbg("[SocketClient] Connecting to %s:%d", host.c_str(), port);

            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
                notify({ Event::Error, "WSAStartup failed" });
                return false;
            }

            sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (sock == INVALID_SOCKET) {
                WSACleanup();
                notify({ Event::Error, "socket() failed" });
                return false;
            }

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port   = htons((u_short)port);

            if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
                // Try hostname resolution
                addrinfo hints{}, *res = nullptr;
                hints.ai_family   = AF_INET;
                hints.ai_socktype = SOCK_STREAM;
                if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) {
                    closesocket(sock);
                    sock = INVALID_SOCKET;
                    WSACleanup();
                    notify({ Event::Error, "Cannot resolve host: " + host });
                    return false;
                }
                addr.sin_addr = ((sockaddr_in*)res->ai_addr)->sin_addr;
                freeaddrinfo(res);
            }

            // 5-second connect timeout via non-blocking connect
            u_long nonblocking = 1;
            ioctlsocket(sock, FIONBIO, &nonblocking);

            ::connect(sock, (sockaddr*)&addr, sizeof(addr));

            fd_set ws;
            FD_ZERO(&ws);
            FD_SET(sock, &ws);
            timeval tv{ 5, 0 };
            int sel = select(0, nullptr, &ws, nullptr, &tv);

            u_long blocking = 0;
            ioctlsocket(sock, FIONBIO, &blocking);

            if (sel <= 0) {
                closesocket(sock);
                sock = INVALID_SOCKET;
                WSACleanup();
                notify({ Event::Error, "Connection timed out" });
                return false;
            }

            dbg("[SocketClient] Connected");
            running = true;
            worker  = std::thread([this]() { recvLoop(); });

            notify({ Event::Connected, "" });
            return true;
        }

        void disconnect() {

            running = false;

            if (sock != INVALID_SOCKET) {
                shutdown(sock, SD_BOTH);
                closesocket(sock);
                sock = INVALID_SOCKET;
            }

            if (worker.joinable()) worker.join();

            WSACleanup();
        }

        void sendLine(const std::string& msg) {

            if (!isConnected()) return;

            std::string s = msg + "\n";
            int sent = ::send(sock, s.c_str(), (int)s.size(), 0);

            if (sent == SOCKET_ERROR) {
                dbg("[SocketClient] Send failed");
            }
        }

    private:

        void notify(Event e) {
            if (callback) callback(std::move(e));
        }

        void recvLoop() {

            std::string buf;
            char tmp[512];

            while (running) {

                int n = recv(sock, tmp, sizeof(tmp) - 1, 0);

                if (n <= 0) break;

                tmp[n] = '\0';
                buf.append(tmp, n);

                // Deliver complete lines
                size_t pos;
                while ((pos = buf.find('\n')) != std::string::npos) {

                    std::string line = buf.substr(0, pos);
                    buf = buf.substr(pos + 1);

                    if (!line.empty() && line.back() == '\r') line.pop_back();

                    if (!line.empty()) {
                        notify({ Event::Line, line });
                    }
                }
            }

            sock = INVALID_SOCKET;
            dbg("[SocketClient] Disconnected");
            notify({ Event::Disconnected, "" });
        }
    };
}
