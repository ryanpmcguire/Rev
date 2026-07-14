module;

#include <string>
#include <vector>
#include <thread>
#include <functional>
#include <atomic>
#include <cerrno>
#include <cstring>

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#include <dbg.hpp>

export module Rev.SocketClient;

export namespace Rev {

    struct SocketClient {

        struct Event {
            enum Type { Connected, Disconnected, Line, Error } type;
            std::string data;
        };

        using Callback = std::function<void(Event)>;

        int               sock    = -1;
        Callback          callback;
        std::atomic<bool> running { false };
        std::thread       worker;

        // Create without connecting
        SocketClient(Callback cb) : callback(cb) {}

        ~SocketClient() {
            disconnect();
        }

        bool isConnected() const {
            return sock >= 0;
        }

        // Attempt a TCP connection to host:port (blocking, call from non-UI thread)
        bool connect(const std::string& host, int port) {

            dbg("[SocketClient] Connecting to %s:%d", host.c_str(), port);

            sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (sock < 0) {
                notify({ Event::Error, "socket() failed" });
                return false;
            }

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port   = htons((uint16_t)port);

            if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
                // Try hostname resolution
                addrinfo hints{}, *res = nullptr;
                hints.ai_family   = AF_INET;
                hints.ai_socktype = SOCK_STREAM;
                if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) {
                    close(sock);
                    sock = -1;
                    notify({ Event::Error, "Cannot resolve host: " + host });
                    return false;
                }
                addr.sin_addr = ((sockaddr_in*)res->ai_addr)->sin_addr;
                freeaddrinfo(res);
            }

            // 5-second connect timeout via non-blocking connect
            int flags = fcntl(sock, F_GETFL, 0);
            fcntl(sock, F_SETFL, flags | O_NONBLOCK);

            ::connect(sock, (sockaddr*)&addr, sizeof(addr));

            fd_set ws;
            FD_ZERO(&ws);
            FD_SET(sock, &ws);
            timeval tv{ 5, 0 };
            int sel = select(sock + 1, nullptr, &ws, nullptr, &tv);

            fcntl(sock, F_SETFL, flags);

            int soerr = 0;
            socklen_t len = sizeof(soerr);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &soerr, &len);

            if (sel <= 0 || soerr != 0) {
                close(sock);
                sock = -1;
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

            if (sock >= 0) {
                shutdown(sock, SHUT_RDWR);
                close(sock);
                sock = -1;
            }

            if (worker.joinable()) worker.join();
        }

        void sendLine(const std::string& msg) {

            if (!isConnected()) return;

            std::string s = msg + "\n";
            ssize_t sent = ::send(sock, s.c_str(), s.size(), MSG_NOSIGNAL);

            if (sent < 0) {
                dbg("[SocketClient] Send failed: %s", strerror(errno));
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

                ssize_t n = recv(sock, tmp, sizeof(tmp) - 1, 0);

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

            sock = -1;
            dbg("[SocketClient] Disconnected");
            notify({ Event::Disconnected, "" });
        }
    };
}
