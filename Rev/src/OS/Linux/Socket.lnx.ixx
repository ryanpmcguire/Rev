module;

#include <atomic>
#include <cerrno>
#include <cstring>
#include <functional>
#include <netinet/in.h>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <dbg.hpp>

export module Rev.Socket;

export namespace Rev {

    struct Socket {

        struct NetEvent {
            enum Type { Connect, Disconnect, Data, Error } type;
            void* subject = nullptr;
            int client = -1;
            std::vector<char> data;
        };

        using Callback = std::function<void(NetEvent&)>;

        int server = -1;
        std::vector<int> clients;
        Callback callback;

        std::atomic<bool> running = false;
        std::thread worker;

        int port = 0;

        Socket(int port, Callback cb) : port(port), callback(cb) {
            dbg("[Socket] Starting on port %d", port);

            server = socket(AF_INET, SOCK_STREAM, 0);
            if (server < 0) {
                dbg("[Socket] Failed to create socket: %s", strerror(errno));
                return;
            }

            int yes = 1;
            setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            addr.sin_addr.s_addr = INADDR_ANY;

            if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
                dbg("[Socket] Bind failed: %s", strerror(errno));
                close(server);
                server = -1;
                return;
            }

            if (listen(server, SOMAXCONN) < 0) {
                dbg("[Socket] Listen failed: %s", strerror(errno));
                close(server);
                server = -1;
                return;
            }

            dbg("[Socket] Listening...");

            running = true;
            worker = std::thread([this]() { this->loop(); });
        }

        ~Socket() {
            dbg("[Socket] Shutting down");

            running = false;
            if (server >= 0) shutdown(server, SHUT_RDWR);
            if (worker.joinable()) worker.join();

            for (auto c : clients) {
                dbg("[Socket] Closing client (%d)", c);
                close(c);
            }

            if (server >= 0) close(server);
        }

        void notify(NetEvent e) {
            e.subject = this;
            if (callback) callback(e);
        }

        void loop() {
            while (running) {
                fd_set readfds;
                FD_ZERO(&readfds);

                int maxfd = server;
                if (server >= 0) FD_SET(server, &readfds);

                for (auto c : clients) {
                    FD_SET(c, &readfds);
                    if (c > maxfd) maxfd = c;
                }

                int activity = select(maxfd + 1, &readfds, nullptr, nullptr, nullptr);
                if (activity <= 0) continue;

                if (server >= 0 && FD_ISSET(server, &readfds)) {
                    int client = accept(server, nullptr, nullptr);
                    if (client >= 0) {
                        clients.push_back(client);
                        dbg("[Socket] Client connected (%d)", client);
                        notify({ NetEvent::Connect, this, client });
                    }
                }

                for (size_t i = 0; i < clients.size(); i++) {
                    int c = clients[i];
                    if (!FD_ISSET(c, &readfds)) continue;

                    char buffer[4096];
                    ssize_t bytes = recv(c, buffer, sizeof(buffer) - 1, 0);

                    if (bytes <= 0) {
                        dbg("[Socket] Client disconnected (%d)", c);
                        notify({ NetEvent::Disconnect, this, c });
                        close(c);
                        clients.erase(clients.begin() + i);
                        i--;
                        continue;
                    }

                    buffer[bytes] = '\0';

                    dbg("--------------------------------------------------");
                    dbg("[Socket] Received (%d bytes) from (%d):", (int)bytes, c);
                    dbg("%s", buffer);
                    dbg("--------------------------------------------------");

                    NetEvent e;
                    e.type = NetEvent::Data;
                    e.client = c;
                    e.data.assign(buffer, buffer + bytes);
                    notify(e);

                    std::string response =
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/plain\r\n"
                        "Content-Length: 12\r\n"
                        "Connection: close\r\n"
                        "\r\n"
                        "Hello world";

                    sendText(c, response);
                    i--;
                }
            }
        }

        void sendText(int client, const std::string& msg) {
            if (client < 0) {
                dbg("[Socket] Attempted send on invalid client");
                return;
            }

            ssize_t result = ::send(client, msg.c_str(), msg.size(), MSG_NOSIGNAL);

            if (result < 0) dbg("[Socket] Send failed (%d): %s", client, strerror(errno));
            else dbg("[Socket] Sent %d bytes to (%d)", (int)result, client);
        }
    };
};
