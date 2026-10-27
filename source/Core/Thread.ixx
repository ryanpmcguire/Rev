module;

#include <atomic>
#include <functional>
#include <stop_token>
#include <thread>
#include <utility>

export module Rev.Core.Thread;

export namespace Rev::Core {

    // A long-lived, cooperatively stoppable worker. Unlike Promise, Thread
    // does not pump completion onto Process and is suitable for streaming I/O.
    struct Thread {
        using Work = std::function<void(std::stop_token)>;

        Thread() = default;
        explicit Thread(Work work);
        Thread(const Thread&) = delete;
        Thread& operator=(const Thread&) = delete;
        ~Thread();

        bool start(Work work);
        void requestStop();
        void join();
        void stop();
        bool joinable() const;
        bool running() const;
        std::thread::id id() const;

    private:
        std::jthread worker;
        std::atomic<bool> active = false;
    };

    Thread::Thread(Work work) { start(std::move(work)); }
    Thread::~Thread() { stop(); }

    bool Thread::start(Work work) {
        if (!work || active.load(std::memory_order_acquire)) { return false; }
        stop();
        active.store(true, std::memory_order_release);
        worker = std::jthread([
            this, work = std::move(work)
        ](std::stop_token stopToken) mutable {
            work(stopToken);
            active.store(false, std::memory_order_release);
        });
        return true;
    }

    void Thread::requestStop() {
        if (worker.joinable()) { worker.request_stop(); }
    }

    void Thread::join() {
        if (
            worker.joinable() &&
            std::this_thread::get_id() != worker.get_id()
        ) {
            worker.join();
        }
        active.store(false, std::memory_order_release);
    }

    void Thread::stop() {
        requestStop();
        join();
    }

    bool Thread::joinable() const { return worker.joinable(); }
    bool Thread::running() const {
        return active.load(std::memory_order_acquire);
    }
    std::thread::id Thread::id() const { return worker.get_id(); }
}
