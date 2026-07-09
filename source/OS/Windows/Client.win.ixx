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

import Rev.Core.Dispatcher;
import Rev.Core.Process;

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

            // Stop the self-pump first; the main loop must not tick into a
            // half-destroyed client. (Same thread as the destructor, so safe.)
            Core::Process::instance().unschedule(this);

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

            // Drain the worker's queue on the main thread from here on. Scheduling
            // a Process tick also keeps the main loop awake while connected, so
            // inbound events reflect promptly without waiting on user input.
            Core::Process::instance().schedule(this, PumpIntervalMs, [this](uint64_t) { pump(); });
        }

        // -- API ---------------------------------------------------------

        // Keep-alive: while connected, the worker sends `message` every
        // `intervalMs` to stop an idle peer from dropping the link (and, for
        // status-poll protocols, to drive telemetry). intervalMs == 0 disables it.
        // Configure BEFORE connect(); the message is read on the worker thread.
        void setHeartbeat(const std::string& message, int intervalMs) {
            heartbeatMessage_     = message;
            heartbeatIntervalMs_  = intervalMs;
        }

        // Drain queued inbound events and dispatch them on THIS (the caller's)
        // thread -- which is the main thread, since pump() is driven by Process.
        // The worker only fills the queue; nothing it produces reaches a listener
        // until here, so all listeners run single-threaded. Public so a same-
        // threaded client could also be pumped by hand.
        void pump() {

            std::vector<Pending> batch;
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                batch.swap(queue_);
            }

            for (Pending& p : batch) {
                switch (p.kind) {
                    case Pending::Kind::Connecting: { ConnectingEvent e{ p.text };                  connectingDispatcher_.tell(&Client::connectingEvent, e); break; }
                    case Pending::Kind::Connect:    { ConnectEvent    e{ p.text };                  connectDispatcher_.tell   (&Client::connectEvent,    e); break; }
                    case Pending::Kind::Disconnect: { DisconnectEvent e{};                          disconnectDispatcher_.tell(&Client::disconnectEvent, e); break; }
                    case Pending::Kind::Data:       { DataEvent       e; e.data = std::move(p.data); dataDispatcher_.tell      (&Client::dataEvent,       e); break; }
                    case Pending::Kind::Error:      { ErrorEvent      e{ p.text };                  errorDispatcher_.tell     (&Client::errorEvent,      e); break; }
                }
            }
        }

        void send(const std::string& msg) {
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

        void disconnect() {

            // Nothing to do if no worker was ever started / we are already down.
            if (!running && !worker_.joinable()) { return; }

            // Were we closing a LIVE link? If running is already false, the worker
            // exited on its own (remote close / failed connect) and already
            // announced the disconnect -- we must not announce a second one.
            const bool wasActive = running;

            running     = false;
            isConnected = false;

            SOCKET s = sock_.load();
            if (s != INVALID_SOCKET) {
                shutdown(s, SD_BOTH);
                closesocket(s);
                sock_.store(INVALID_SOCKET);
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
            Core::Process::instance().unschedule(this);
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
        std::mutex           sendMutex_;   // serializes the raw ::send() across threads

        // -- Heartbeat (set before connect; read on the worker thread) ---

        std::string          heartbeatMessage_;
        std::atomic<int>     heartbeatIntervalMs_ { 0 };   // 0 = disabled

        // -- Inbound queue: the marshal across threads -------------------
        // The worker thread NEVER dispatches; it enqueues decoded events here
        // under the mutex. The main thread drains them in pump(), so every
        // listener -- and everything downstream of it -- runs single-threaded.

        struct Pending {
            enum class Kind { Connecting, Connect, Disconnect, Data, Error };
            Kind              kind;
            std::string       text;   // address (connect/connecting) or reason (error)
            std::vector<char> data;   // payload (data)
        };

        std::mutex           queueMutex_;
        std::vector<Pending> queue_;

        static constexpr uint64_t PumpIntervalMs = 16;   // ~60 Hz drain on the main thread

        void enqueue(Pending p) {
            std::lock_guard<std::mutex> lock(queueMutex_);
            queue_.push_back(std::move(p));
        }

        // -- Fire helpers (called from the worker thread): enqueue, never dispatch --

        void fireConnecting(const std::string& address) { enqueue({ Pending::Kind::Connecting, address, {} }); }
        void fireConnect   (const std::string& address) { enqueue({ Pending::Kind::Connect,    address, {} }); }
        void fireDisconnect()                            { enqueue({ Pending::Kind::Disconnect, {},      {} }); }
        void fireError     (std::string reason)          { enqueue({ Pending::Kind::Error, std::move(reason), {} }); }

        void fireData(const char* buf, int len) {
            Pending p{ Pending::Kind::Data, {}, {} };
            p.data.assign(buf, buf + len);
            enqueue(std::move(p));
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
