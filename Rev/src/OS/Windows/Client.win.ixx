module;

#include <string>
#include <vector>
#include <thread>
#include <functional>
#include <atomic>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <dbg.hpp>

export module Rev.Client;

export namespace Rev {

    // ------------------------------------------------------------------
    // Client  — outbound TCP connection.
    //
    // Usage mirrors Rev.Socket but inverted: instead of listening for
    // incoming connections, Client dials out to a remote host:port.
    //
    //   Rev::Client* c = new Rev::Client("192.168.1.10", 2222,
    //       [](Rev::Client::NetEvent& e) {
    //           if (e.type == Rev::Client::NetEvent::Data) { ... }
    //       });
    //   c->send("G0 X10\n");
    //   delete c; // disconnects cleanly
    // ------------------------------------------------------------------

    struct Client {

        struct NetEvent {
            enum Type { Connect, Disconnect, Data, Error } type;
            std::vector<char> data;
        };

        using Callback = std::function<void(NetEvent&)>;

        SOCKET sock    = INVALID_SOCKET;
        Callback       callback;
        std::string    host;
        int            port  = 0;

        std::atomic<bool> running = false;
        std::thread worker;

        // -- Construction / destruction -----------------------------------

        Client(std::string host, int port, Callback cb)
            : host(std::move(host)), port(port), callback(std::move(cb))
        {
            dbg("[Client] Connecting to %s:%d", this->host.c_str(), this->port);

            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
                dbg("[Client] WSAStartup failed");
                fire({ NetEvent::Error });
                return;
            }

            sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock == INVALID_SOCKET) {
                dbg("[Client] Failed to create socket");
                WSACleanup();
                fire({ NetEvent::Error });
                return;
            }

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port   = htons(this->port);
            inet_pton(AF_INET, this->host.c_str(), &addr.sin_addr);

            if (::connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
                dbg("[Client] Connect failed (%d)", WSAGetLastError());
                closesocket(sock);
                sock = INVALID_SOCKET;
                WSACleanup();
                fire({ NetEvent::Error });
                return;
            }

            dbg("[Client] Connected to %s:%d", this->host.c_str(), this->port);
            fire({ NetEvent::Connect });

            running = true;
            worker  = std::thread([this]() { loop(); });
        }

        ~Client() {
            dbg("[Client] Shutting down");

            running = false;

            if (sock != INVALID_SOCKET) {
                shutdown(sock, SD_BOTH);
                closesocket(sock);
                sock = INVALID_SOCKET;
            }

            if (worker.joinable()) worker.join();

            WSACleanup();
        }

        // -- API ----------------------------------------------------------

        void send(const std::string& msg) {
            if (sock == INVALID_SOCKET) {
                dbg("[Client] send() called on closed socket");
                return;
            }
            int result = ::send(sock, msg.c_str(), (int)msg.size(), 0);
            if (result == SOCKET_ERROR) {
                dbg("[Client] send failed (%d)", WSAGetLastError());
            } else {
                dbg("[Client] Sent %d bytes", result);
            }
        }

        void disconnect() {
            running = false;
            if (sock != INVALID_SOCKET) {
                shutdown(sock, SD_BOTH);
                closesocket(sock);
                sock = INVALID_SOCKET;
            }
        }

        bool connected() const {
            return sock != INVALID_SOCKET && running.load();
        }

        // -- Internal -----------------------------------------------------

    private:

        void fire(NetEvent e) {
            if (callback) callback(e);
        }

        void loop() {
            while (running) {

                fd_set readfds;
                FD_ZERO(&readfds);
                FD_SET(sock, &readfds);

                // 100 ms timeout so we can check `running` periodically.
                timeval tv{ .tv_sec = 0, .tv_usec = 100'000 };
                int activity = select(0, &readfds, nullptr, nullptr, &tv);

                if (activity < 0) {
                    dbg("[Client] select error (%d)", WSAGetLastError());
                    fire({ NetEvent::Error });
                    break;
                }

                if (activity == 0) continue; // timeout — loop back

                char buffer[4096];
                int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);

                if (bytes <= 0) {
                    dbg("[Client] Remote closed connection");
                    fire({ NetEvent::Disconnect });
                    break;
                }

                buffer[bytes] = '\0';
                dbg("[Client] Received %d bytes: %s", bytes, buffer);

                NetEvent e;
                e.type = NetEvent::Data;
                e.data.assign(buffer, buffer + bytes);
                fire(e);
            }

            running = false;
        }
    };
}
