module;

#include <string>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <thread>
#include <functional>
#include <atomic>

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

        SOCKET server = INVALID_SOCKET;
        std::vector<SOCKET> clients;
        Callback callback;

        std::atomic<bool> running = false;
        std::thread worker;

        int port = 0;

        Socket(int port, Callback cb) : port(port), callback(cb) {

            dbg("[Socket] Starting on port %d", port);

            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
                dbg("[Socket] WSAStartup failed");
                return;
            }

            server = socket(AF_INET, SOCK_STREAM, 0);
            if (server == INVALID_SOCKET) {
                dbg("[Socket] Failed to create socket");
                return;
            }

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            addr.sin_addr.s_addr = INADDR_ANY;

            if (bind(server, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
                dbg("[Socket] Bind failed");
                return;
            }

            if (listen(server, SOMAXCONN) == SOCKET_ERROR) {
                dbg("[Socket] Listen failed");
                return;
            }

            dbg("[Socket] Listening...");

            running = true;
            worker = std::thread([this]() { this->loop(); });
        }

        ~Socket() {
            dbg("[Socket] Shutting down");

            running = false;
            if (worker.joinable()) worker.join();

            for (auto c : clients) {
                dbg("[Socket] Closing client (%d)", (int)c);
                closesocket(c);
            }

            closesocket(server);
            WSACleanup();
        }

        void notify(NetEvent e) {
            e.subject = this;
            if (callback) callback(e);
        }

        void loop() {

            fd_set readfds;

            while (running) {

                FD_ZERO(&readfds);
                FD_SET(server, &readfds);

                for (auto c : clients) {
                    FD_SET(c, &readfds);
                }

                int activity = select(0, &readfds, nullptr, nullptr, nullptr);
                if (activity <= 0) continue;

                // --------------------------------------------------
                // New connection
                // --------------------------------------------------
                if (FD_ISSET(server, &readfds)) {

                    SOCKET client = accept(server, nullptr, nullptr);
                    if (client != INVALID_SOCKET) {

                        clients.push_back(client);
                        dbg("[Socket] Client connected (%d)", (int)client);

                        notify({ NetEvent::Connect, this, (int)client });
                    }
                }

                // --------------------------------------------------
                // Existing clients
                // --------------------------------------------------
                for (size_t i = 0; i < clients.size(); i++) {

                    SOCKET c = clients[i];

                    if (!FD_ISSET(c, &readfds)) continue;

                    char buffer[4096];
                    int bytes = recv(c, buffer, sizeof(buffer) - 1, 0);

                    // ----------------------------------------------
                    // Disconnect
                    // ----------------------------------------------
                    if (bytes <= 0) {

                        dbg("[Socket] Client disconnected (%d)", (int)c);

                        notify({ NetEvent::Disconnect, this, (int)c });

                        closesocket(c);
                        clients.erase(clients.begin() + i);
                        i--;
                        continue;
                    }

                    // ----------------------------------------------
                    // Received data (HTTP plaintext)
                    // ----------------------------------------------
                    buffer[bytes] = '\0'; // safe null-termination

                    dbg("--------------------------------------------------");
                    dbg("[Socket] Received (%d bytes) from (%d):", bytes, (int)c);
                    dbg("%s", buffer);
                    dbg("--------------------------------------------------");

                    // Notify system (raw data preserved)
                    NetEvent e;
                    e.type = NetEvent::Data;
                    e.client = (int)c;
                    e.data.assign(buffer, buffer + bytes);
                    notify(e);

                    // ----------------------------------------------
                    // Minimal HTTP response (so browser doesn't hang)
                    // ----------------------------------------------
                    std::string response =
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/plain\r\n"
                        "Content-Length: 12\r\n"
                        "Connection: close\r\n"
                        "\r\n"
                        "Hello world";

                    sendText(c, response);

                    // Close after response (simple HTTP 1.0-style handling)
                    //closesocket(c);
                    //clients.erase(clients.begin() + i);
                    i--;
                }
            }
        }

        void sendText(int client, const std::string& msg) {

            if (client == INVALID_SOCKET) {
                dbg("[Socket] Attempted send on invalid client");
                return;
            }

            int result = ::send((SOCKET)client, msg.c_str(), (int)msg.size(), 0);

            if (result == SOCKET_ERROR) {
                dbg("[Socket] Send failed (%d)", (int)client);
            }
            else {
                dbg("[Socket] Sent %d bytes to (%d)", result, (int)client);
            }
        }
    };
};