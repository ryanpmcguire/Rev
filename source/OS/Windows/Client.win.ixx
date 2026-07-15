module;

#include <string>
#include <vector>
#include <thread>
#include <functional>
#include <atomic>
#include <chrono>
#include <mutex>
#include <cstdint>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <dbg.hpp>

export module Rev.Client;

import Rev.Transport;
import Rev.Core.Process;

export namespace Rev {

    // ------------------------------------------------------------------
    // Client  — outbound TCP connection; the winsock implementation of the
    // Rev::Transport byte-pipe. All the shared marshaling machinery (event
    // types, dispatchers, inbound queue, pump) lives in Transport; this class
    // only adds the socket worker + heartbeat.
    //
    // Construction is separated from connection, so listeners can be
    // registered before the thread starts (Node.js style):
    //
    //   Rev::Client* c = new Rev::Client();
    //   c->onConnect   ([](Rev::Transport::ConnectEvent&    e) { ... });
    //   c->onData      ([](Rev::Transport::DataEvent&       e) { ... });
    //   c->onDisconnect([](Rev::Transport::DisconnectEvent& e) { ... });
    //   c->onError     ([](Rev::Transport::ErrorEvent&      e) { ... });
    //   c->connect("192.168.1.10", 2222);
    //
    //   c->send("G0 X10\n");
    //   delete c;  // disconnects and joins the worker thread
    // ------------------------------------------------------------------

    struct Client : public Transport {

        // -- Construction / destruction -----------------------------------

        Client() = default;

        ~Client() override {
            dbg("[Client] Shutting down");

            // Stop the self-pump first; the main loop must not tick into a
            // half-destroyed client. (Same thread as the destructor, so safe.)
            stopPump();

            running     = false;
            isConnected = false;

            // Atomic exchange: exactly one thread ever closes the socket.
            SOCKET s = sock_.exchange(INVALID_SOCKET);
            if (s != INVALID_SOCKET) {
                shutdown(s, SD_BOTH);
                closesocket(s);
            }

            if (worker_.joinable()) worker_.join();

            if (wsaStarted_) {
                WSACleanup();
                wsaStarted_ = false;
            }
        }

        // -- Connection --------------------------------------------------

        void connect(std::string host, int port) override {

            if (running) {
                dbg("[Client] connect() called while already running");
                return;
            }

            // A previous worker may have exited on its OWN -- a failed connect, or
            // a remote close (recvLoop fires disconnect and returns) -- leaving the
            // thread joinable but unjoined. Assigning a fresh std::thread over a
            // joinable one calls std::terminate, so join the old worker first. This
            // is the one invariant the whole lifecycle rests on: worker_ is never
            // joinable at the moment we assign to it.
            if (worker_.joinable()) {
                worker_.join();
            }

            host_ = std::move(host);
            port_ = port;

            running     = true;
            isConnected = false;

            worker_ = std::thread([this]() { workerMain(); });

            // Drain the worker's queue on the main thread from here on.
            startPump();
        }

        // -- API ---------------------------------------------------------
        // setHeartbeat() is inherited from Transport (shared by all links).

        void send(const std::string& msg) override {
            SOCKET s = sock_.load();
            if (s == INVALID_SOCKET) {
                dbg("[Client] send() on closed socket");
                return;
            }
            // send() is reachable from both the worker (heartbeat) and the main thread
            // (posted commands); serialize the raw socket write so a pulse and a command
            // line can't interleave on the wire.
            std::lock_guard<std::mutex> lock(sendMutex_);
            int result = ::send(s, msg.c_str(), (int)msg.size(), 0);
            if (result == SOCKET_ERROR) {
                dbg("[Client] send failed (%d)", WSAGetLastError());
            } else {
                //dbg("[Client] Sent %d bytes", result);
            }
        }

        void disconnect() override {

            // Nothing to do if no worker was ever started / we are already down.
            if (!running && !worker_.joinable()) { return; }

            // Were we closing a LIVE link? If running is already false, the worker
            // exited on its own (remote close / failed connect) and already
            // announced the disconnect -- we must not announce a second one.
            const bool wasActive = running;

            running     = false;
            isConnected = false;

            // Atomic exchange: exactly one thread ever closes the socket.
            SOCKET s = sock_.exchange(INVALID_SOCKET);
            if (s != INVALID_SOCKET) {
                shutdown(s, SD_BOTH);
                closesocket(s);
            }

            // Join the worker so a later connect() can safely start a fresh one
            // (assigning over a joinable std::thread calls std::terminate). Guard
            // against joining ourselves if ever called from inside a callback.
            if (worker_.joinable() && std::this_thread::get_id() != worker_.get_id()) {
                worker_.join();
            }

            // The recv loop exits silently on OUR own close, so a user-initiated
            // disconnect of a live link would otherwise go unannounced. Announce it
            // here (on the caller's thread) -- but only if it was actually live.
            if (wasActive) {
                fireDisconnect();
            }

            // Drain whatever is still queued (incl. the disconnect just enqueued)
            // on this main thread, then stop the self-pump.
            pump();
            stopPump();
        }

    private:

        // -- Worker state ------------------------------------------------

        std::string          host_;
        int                  port_       = 0;
        std::atomic<SOCKET>  sock_       { INVALID_SOCKET };
        std::thread          worker_;
        bool                 wsaStarted_ = false;
        std::mutex           sendMutex_;   // serializes the raw ::send() across threads

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
                SOCKET cur = sock_.exchange(INVALID_SOCKET);
                if (cur != INVALID_SOCKET) { closesocket(cur); }
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
            SOCKET current = sock_.exchange(INVALID_SOCKET);
            if (current != INVALID_SOCKET) { closesocket(current); }
            running = false;
        }

        void recvLoop(SOCKET s) {

            auto lastBeat = std::chrono::steady_clock::now();

            while (running) {

                fd_set readfds;
                FD_ZERO(&readfds);
                FD_SET(s, &readfds);

                timeval tv{ .tv_sec = 0, .tv_usec = 100'000 };
                int activity = select(0, &readfds, nullptr, nullptr, &tv);

                if (!running) break;

                // Heartbeat: keep the link alive at the configured cadence. The
                // 100ms select tick is our timer; we send when the interval is up.
                const int interval = heartbeatIntervalMs_.load();
                if (interval > 0) {
                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastBeat).count() >= interval) {
                        send(heartbeatMessage_);
                        lastBeat = now;
                    }
                }

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
