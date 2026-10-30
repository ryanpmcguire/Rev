module;

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <memory>

#include <dbg.hpp>

export module Rev.Transport;

import Rev.Core.Dispatcher;
import Rev.Core.Promise;

export namespace Rev {

    // ------------------------------------------------------------------
    // Transport  — the byte-pipe seam shared by every link (TCP, serial).
    //
    // I/O workers enqueue events and wake the Promise executor immediately.
    // Protocol listeners run on that executor; GUI observers use listenMain.
    //
    // A concrete transport implements the three transport-SPECIFIC verbs
    // (connect / send / disconnect) with its own worker thread, calling the
    // inherited fire* helpers to push inbound events across the thread boundary.
    // ------------------------------------------------------------------

    struct Transport {

        // -- Event types (shared by all transports) ----------------------

        struct ConnectingEvent  { std::string address; };
        struct ConnectEvent     { std::string address; };
        struct DisconnectEvent  {};
        struct DataEvent        { std::vector<char> data; };
        struct ErrorEvent       { std::string reason; };

        // -- Public state ------------------------------------------------

        std::atomic<bool> running     = false;
        std::atomic<bool> isConnected = false;

        Transport() = default;
        virtual ~Transport() { stopPump(); }

        // -- Registration (call before connect) --------------------------

        void onConnecting(std::function<void(ConnectingEvent&)> f) { connectingDispatcher_.listen(&Transport::connectingEvent, f); }
        void onConnect   (std::function<void(ConnectEvent&)>    f) { connectDispatcher_.listen   (&Transport::connectEvent,    f); }
        void onDisconnect(std::function<void(DisconnectEvent&)> f) { disconnectDispatcher_.listen(&Transport::disconnectEvent, f); }
        void onData      (std::function<void(DataEvent&)>       f) { dataDispatcher_.listen       (&Transport::dataEvent,       f); }
        void onError     (std::function<void(ErrorEvent&)>      f) { errorDispatcher_.listen      (&Transport::errorEvent,      f); }

        // -- Transport verbs (implemented per link) ----------------------
        //
        // connect() takes (endpoint, param): host + port for TCP, COM port +
        // baud for serial. Both are (std::string, int), so one signature serves
        // every transport and the adapter can hold a Transport* uniformly.

        virtual void connect(std::string endpoint, int param) = 0;
        virtual void send(const std::string& msg)              = 0;
        virtual void disconnect()                              = 0;

        // Keep-alive: while connected, the worker sends `message` every
        // `intervalMs` to stop an idle peer from dropping the link (and, for
        // status-poll protocols, to drive telemetry). intervalMs == 0 disables
        // it. Configure BEFORE connect(); read on the worker thread. Shared by
        // every transport; each worker loop drives the cadence via maybeBeat().
        void setHeartbeat(const std::string& message, int intervalMs) {
            heartbeatMessage_    = message;
            heartbeatIntervalMs_ = intervalMs;
        }

        // -- Pump: drain queued inbound events on the caller's (main) thread --
        // The worker only fills the queue; nothing it produces reaches a
        // listener until here, so all listeners run single-threaded.

        void pump() {
            if (!Core::PromiseManager::instance().isPromiseThread()) {
                scheduleDrain();
                return;
            }

            std::vector<Pending> batch;
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                batch.swap(queue_);
            }

            for (Pending& p : batch) {
                switch (p.kind) {
                    case Pending::Kind::Connecting: { ConnectingEvent e{ p.text };                  connectingDispatcher_.tell(&Transport::connectingEvent, e); break; }
                    case Pending::Kind::Connect:    { ConnectEvent    e{ p.text };                  connectDispatcher_.tell   (&Transport::connectEvent,    e); break; }
                    case Pending::Kind::Disconnect: { DisconnectEvent e{};                          disconnectDispatcher_.tell(&Transport::disconnectEvent, e); break; }
                    case Pending::Kind::Data:       { DataEvent       e; e.data = std::move(p.data); dataDispatcher_.tell      (&Transport::dataEvent,       e); break; }
                    case Pending::Kind::Error:      { ErrorEvent      e{ p.text };                  errorDispatcher_.tell     (&Transport::errorEvent,      e); break; }
                }
            }
        }

        // -- Internal virtual event slots (Dispatcher keys) --------------

    protected:

        virtual void connectingEvent(ConnectingEvent&)  {}
        virtual void connectEvent   (ConnectEvent&)     {}
        virtual void disconnectEvent(DisconnectEvent&)  {}
        virtual void dataEvent      (DataEvent&)        {}
        virtual void errorEvent     (ErrorEvent&)       {}

        // -- Dispatchers -------------------------------------------------

        Core::Dispatcher<ConnectingEvent>  connectingDispatcher_;
        Core::Dispatcher<ConnectEvent>     connectDispatcher_;
        Core::Dispatcher<DisconnectEvent>  disconnectDispatcher_;
        Core::Dispatcher<DataEvent>        dataDispatcher_;
        Core::Dispatcher<ErrorEvent>       errorDispatcher_;

        // -- Inbound queue: the marshal across threads -------------------

        struct Pending {
            enum class Kind { Connecting, Connect, Disconnect, Data, Error };
            Kind              kind;
            std::string       text;   // address (connect/connecting) or reason (error)
            std::vector<char> data;   // payload (data)
        };

        std::mutex           queueMutex_;
        std::vector<Pending> queue_;

        struct Delivery {
            std::recursive_mutex mutex;
            Transport* owner = nullptr;
            std::atomic<bool> queued = false;
        };
        std::shared_ptr<Delivery> delivery_ = std::make_shared<Delivery>();

        void scheduleDrain() {
            auto state = delivery_;
            if (state->queued.exchange(true)) return;
            Core::Async::Do([state]() -> Core::Promise<void> {
                std::lock_guard lock(state->mutex);
                state->queued = false;
                if (state->owner) state->owner->pump();
                co_return;
            });
        }

        void enqueue(Pending p) {
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                queue_.push_back(std::move(p));
            }
            scheduleDrain();
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

        // No GUI timer: registration only controls lifetime of queued delivery.
        void startPump() {
            { std::lock_guard lock(delivery_->mutex); delivery_->owner = this; }
            scheduleDrain();
        }
        void stopPump() {
            std::lock_guard lock(delivery_->mutex);
            delivery_->owner = nullptr;
        }

        // -- Heartbeat (set before connect; read on the worker thread) ---

        std::string      heartbeatMessage_;
        std::atomic<int> heartbeatIntervalMs_ { 0 };   // 0 = disabled
    };
}
