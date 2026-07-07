module;

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

export module Rev.Core.Promise;

import Rev.Core.Dispatcher;
import Rev.Core.Process;

export namespace Rev::Core {

    // ------------------------------------------------------------------
    // Promise — a buffer between a worker thread and the main thread.
    //
    // The worker never executes the dispatcher; it settles the container,
    // and the main thread announces "done" to subscribers on its own next
    // pump. Listeners therefore always run single-threaded.
    //
    //   Rev::Core::Promise* p = new Rev::Core::Promise();
    //   p->func([p]() { while (working) { p->checkpoint(); step(); } });
    //   p->await([](Rev::Core::Promise::DoneEvent& e) { ... });
    //   p->start();
    //
    //   p->pause();   // worker blocks at its next checkpoint()
    //   p->resume();
    //   p->kill();    // worker exits at its next checkpoint()
    //   delete p;     // kills and joins
    //
    // Control methods (func/start/pause/resume/kill) are main-thread.
    // checkpoint() is the worker's side of the contract: call it inside
    // long work to honor pause/kill; work that never checkpoints simply
    // runs to completion.
    // ------------------------------------------------------------------

    struct Promise {

        // -- Event types -------------------------------------------------

        struct DoneEvent {};

        // -- Public state ------------------------------------------------

        enum class State { Idle, Running, Paused, Done, Killed };
        State state = State::Idle;

        // -- Construction / destruction -----------------------------------

        Promise() = default;

        Promise(std::function<void()> f) {
            func(std::move(f));
        }

        ~Promise() {
            Process::instance().unschedule(this);
            killFlag = true;
            wake();
            if (worker.joinable()) { worker.join(); }
        }

        // -- Registration (call before start) -----------------------------

        void func(std::function<void()> f) {
            work = std::move(f);
        }

        // Called on the main thread when the work finishes. A subscriber
        // arriving after completion fires immediately with the settled state.
        void await(std::function<void(DoneEvent&)> f) {
            if (state == State::Done) { DoneEvent e; f(e); return; }
            doneDispatcher.listen(&Promise::doneEvent, f);
        }

        void await(void* owner, std::function<void(DoneEvent&)> f) {
            if (state == State::Done) { DoneEvent e; f(e); return; }
            doneDispatcher.listen(&Promise::doneEvent, owner, f);
        }

        void unsubscribe(void* owner) {
            doneDispatcher.unsubscribe(owner);
        }

        // -- Control -------------------------------------------------------

        void start() {

            if (!work) { return; }
            if (state == State::Running || state == State::Paused) { return; }

            // A prior run's thread may be finished but unjoined; assigning a
            // fresh std::thread over a joinable one calls std::terminate.
            if (worker.joinable()) { worker.join(); }

            killFlag  = false;
            pauseFlag = false;
            settled   = false;
            state     = State::Running;

            // The pump doubles as the keep-awake: while the promise is pending
            // the main loop keeps ticking, so settlement reflects promptly.
            Process::instance().schedule(this, PumpIntervalMs, [this](uint64_t) { pump(); });

            worker = std::thread([this]() {
                try { work(); } catch (Killed&) {}
                settled.store(true, std::memory_order_release);
            });
        }

        // Takes effect at the worker's next checkpoint().
        void pause() {
            if (state != State::Running) { return; }
            pauseFlag = true;
            state = State::Paused;
        }

        void resume() {
            if (state != State::Paused) { return; }
            {
                std::lock_guard<std::mutex> lock(gate);
                pauseFlag = false;
            }
            wake();
            state = State::Running;
        }

        // Stop the work at its next checkpoint() and join. No done event
        // fires for a killed run.
        void kill() {

            if (state != State::Running && state != State::Paused) { return; }

            killFlag = true;
            wake();

            if (worker.joinable() && std::this_thread::get_id() != worker.get_id()) {
                worker.join();
            }

            Process::instance().unschedule(this);
            state = State::Killed;
        }

        // -- Worker side ---------------------------------------------------

        // Call inside long work: blocks while paused, exits the work (by
        // throwing through it, caught by the wrapper) when killed.
        void checkpoint() {

            if (killFlag) { throw Killed{}; }

            if (pauseFlag) {
                std::unique_lock<std::mutex> lock(gate);
                waiter.wait(lock, [this]() { return !pauseFlag || killFlag; });
                if (killFlag) { throw Killed{}; }
            }
        }

        // -- Internal virtual event slots (Dispatcher keys) ----------------

    protected:

        virtual void doneEvent(DoneEvent&) {}

    private:

        struct Killed {};

        void wake() {
            waiter.notify_all();
        }

        // Main thread, via Process: notices settlement, reclaims the thread,
        // announces done, and cancels its own schedule.
        void pump() {

            if (!settled.load(std::memory_order_acquire)) { return; }

            if (worker.joinable()) { worker.join(); }

            Process::instance().unschedule(this);
            state = killFlag ? State::Killed : State::Done;

            if (state == State::Done) {
                DoneEvent e;
                doneDispatcher.tell(&Promise::doneEvent, e);
            }
        }

        // -- Worker state ----------------------------------------------------

        std::function<void()>   work;
        std::thread             worker;
        std::atomic<bool>       settled   = false;
        std::atomic<bool>       killFlag  = false;
        std::atomic<bool>       pauseFlag = false;
        std::mutex              gate;
        std::condition_variable waiter;

        Dispatcher<DoneEvent> doneDispatcher;

        static constexpr uint64_t PumpIntervalMs = 16;
    };
}
