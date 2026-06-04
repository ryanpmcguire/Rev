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

import Rev.Core.Dispatcher;

export namespace Rev {

    // ------------------------------------------------------------------
    // Client  — outbound TCP connection.
    //
    // Construction is separated from connection, so listeners can be
    // registered before the thread starts (Node.js style):
    //
    //   Rev::Client* c = new Rev::Client();
    //   c->onConnect   ([](Rev::Client::ConnectEvent&    e) { ... });
    //   c->onData      ([](Rev::Client::DataEvent&       e) { ... });
    //   c->onDisconnect([](Rev::Client::DisconnectEvent& e) { ... });
    //   c->onError     ([](Rev::Client::ErrorEvent&      e) { ... });
    //   c->connect("192.168.1.10", 2222);
    //
    //   c->send("G0 X10\n");
    //   delete c;  // disconnects and joins the worker thread
    // ------------------------------------------------------------------

    struct Client {

        // -- Event types -------------------------------------------------

        struct ConnectingEvent  { std::string address; };
        struct ConnectEvent     { std::string address; };
        struct DisconnectEvent  {};
        struct DataEvent        { std::vector<char> data; };
        struct ErrorEvent       { std::string reason; };

        // -- Public state ------------------------------------------------

        std::atomic<bool> running     = false;
        std::atomic<bool> isConnected = false;

        // -- Construction / destruction -----------------------------------

        Client() = default;

        ~Client() {
            dbg("[Client] Shutting down");

            running     = false;
            isConnected = false;

            SOCKET s = sock_.load();
            if (s != INVALID_SOCKET) {
                shutdown(s, SD_BOTH);
                closesocket(s);
                sock_.store(INVALID_SOCKET);
            }

            if (worker_.joinable()) worker_.join();

            if (wsaStarted_) {
                WSACleanup();
                wsaStarted_ = false;
            }
        }

        // -- Registration (call before connect) --------------------------

        void onConnecting(std::function<void(ConnectingEvent&)> f) {
            connectingDispatcher_.listen(&Client::connectingEvent, f);
        }

        void onConnect(std::function<void(ConnectEvent&)> f) {
            connectDispatcher_.listen(&Client::connectEvent, f);
        }

        void onDisconnect(std::function<void(DisconnectEvent&)> f) {
            disconnectDispatcher_.listen(&Client::disconnectEvent, f);
        }

        void onData(std::function<void(DataEvent&)> f) {
            dataDispatcher_.listen(&Client::dataEvent, f);
        }

        void onError(std::function<void(ErrorEvent&)> f) {
            errorDispatcher_.listen(&Client::errorEvent, f);
        }

        // -- Connection --------------------------------------------------

        void connect(std::string host, int port) {

            if (running) {
                dbg("[Client] connect() called while already running");
                return;
            }

            host_ = std::move(host);
            port_ = port;

            running     = true;
            isConnected = false;

            worker_ = std::thread([this]() { workerMain(); });
        }

        // -- API ---------------------------------------------------------

        void send(const std::string& msg) {
            SOCKET s = sock_.load();
            if (s == INVALID_SOCKET) {
                dbg("[Client] send() on closed socket");
                return;
            }
            int result = ::send(s, msg.c_str(), (int)msg.size(), 0);
            if (result == SOCKET_ERROR) {
                dbg("[Client] send failed (%d)", WSAGetLastError());
            } else {
                //dbg("[Client] Sent %d bytes", result);
            }
        }

        void disconnect() {
            running     = false;
            isConnected = false;
            SOCKET s = sock_.load();
            if (s != INVALID_SOCKET) {
                shutdown(s, SD_BOTH);
                closesocket(s);
                sock_.store(INVALID_SOCKET);
            }
        }

        // -- Internal virtual event slots (Dispatcher keys) --------------

    protected:

        virtual void connectingEvent(ConnectingEvent&)  {}
        virtual void connectEvent   (ConnectEvent&)     {}
        virtual void disconnectEvent(DisconnectEvent&)  {}
        virtual void dataEvent      (DataEvent&)        {}
        virtual void errorEvent     (ErrorEvent&)       {}

    private:

        // -- Dispatchers -------------------------------------------------

        Core::Dispatcher<ConnectingEvent>  connectingDispatcher_;
        Core::Dispatcher<ConnectEvent>     connectDispatcher_;
        Core::Dispatcher<DisconnectEvent>  disconnectDispatcher_;
        Core::Dispatcher<DataEvent>        dataDispatcher_;
        Core::Dispatcher<ErrorEvent>       errorDispatcher_;

        // -- Worker state ------------------------------------------------

        std::string          host_;
        int                  port_       = 0;
        std::atomic<SOCKET>  sock_       { INVALID_SOCKET };
        std::thread          worker_;
        bool                 wsaStarted_ = false;

        // -- Fire helpers (call from worker thread) ----------------------

        void fireConnecting(const std::string& address) {
            ConnectingEvent e{ address };
            connectingDispatcher_.tell(&Client::connectingEvent, e);
        }

        void fireConnect(const std::string& address) {
            ConnectEvent e{ address };
            connectDispatcher_.tell(&Client::connectEvent, e);
        }

        void fireDisconnect() {
            DisconnectEvent e{};
            disconnectDispatcher_.tell(&Client::disconnectEvent, e);
        }

        void fireData(const char* buf, int len) {
            DataEvent e;
            e.data.assign(buf, buf + len);
            dataDispatcher_.tell(&Client::dataEvent, e);
        }

        void fireError(std::string reason) {
            ErrorEvent e{ std::move(reason) };
            errorDispatcher_.tell(&Client::errorEvent, e);
        }

        // -- Worker ------------------------------------------------------

        void workerMain() {

            // WSA init
            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
                dbg("[Client] WSAStartup failed");
                fireError("WSAStartup failed");
                running = false;
                return;
            }
            wsaStarted_ = true;

            // Socket
            SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
            if (s == INVALID_SOCKET) {
                dbg("[Client] Failed to create socket");
                fireError("Failed to create socket");
                WSACleanup();
                wsaStarted_ = false;
                running = false;
                return;
            }
            sock_.store(s);

            // Connect
            std::string address = host_ + ":" + std::to_string(port_);
            fireConnecting(address);

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port   = htons(port_);
            inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);

            if (::connect(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
                int err = WSAGetLastError();
                dbg("[Client] Connect failed (%d)", err);
                fireError("Connect failed (" + std::to_string(err) + ")");
                closesocket(s);
                sock_.store(INVALID_SOCKET);
                WSACleanup();
                wsaStarted_ = false;
                running = false;
                return;
            }

            dbg("[Client] Connected to %s", address.c_str());
            isConnected = true;
            fireConnect(address);

            // Recv loop
            recvLoop(s);

            // Cleanup
            isConnected = false;
            SOCKET current = sock_.load();
            if (current != INVALID_SOCKET) {
                closesocket(current);
                sock_.store(INVALID_SOCKET);
            }
            running = false;
        }

        void recvLoop(SOCKET s) {

            while (running) {

                fd_set readfds;
                FD_ZERO(&readfds);
                FD_SET(s, &readfds);

                timeval tv{ .tv_sec = 0, .tv_usec = 100'000 };
                int activity = select(0, &readfds, nullptr, nullptr, &tv);

                if (!running) break;

                if (activity < 0) {
                    dbg("[Client] select error (%d)", WSAGetLastError());
                    fireError("select error");
                    break;
                }

                if (activity == 0) continue;

                char buffer[4096];
                int bytes = recv(s, buffer, sizeof(buffer) - 1, 0);

                if (bytes <= 0) {
                    dbg("[Client] Remote closed connection");
                    fireDisconnect();
                    break;
                }

                buffer[bytes] = '\0';
                //dbg("[Client] Received %d bytes: %s", bytes, buffer);
                fireData(buffer, bytes);
            }
        }
    };
}
