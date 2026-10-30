module;
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <utility>

export module Rev.Core.MainDispatch;
import Rev.Core.Process;

export namespace Rev::Core {
    // A mailbox for observations/rendering only. It never owns coroutine
    // handles and cannot migrate Promise execution onto the GUI thread.
    class MainDispatch {
    public:
        static MainDispatch& instance() { static MainDispatch self; return self; }
        void attach() {
            if (attached.load()) return;
            thread = std::this_thread::get_id();
            attached = true;
            Process::instance().schedule(this, 8, [this](uint64_t) { drain(); });
        }
        bool isMainThread() const { return attached && std::this_thread::get_id() == thread; }
        void setDeliveryBoundary(std::function<void(const std::function<void()>&)> boundary) {
            deliveryBoundary = std::move(boundary);
        }
        void post(std::function<void()> callback) {
            std::lock_guard lock(mutex);
            pending.push_back(std::move(callback));
        }
        void drain() {
            std::deque<std::function<void()>> batch;
            { std::lock_guard lock(mutex); batch.swap(pending); }
            for (auto& callback : batch) {
                if (deliveryBoundary) deliveryBoundary(callback);
                else callback();
            }
        }
    private:
        std::atomic<bool> attached = false;
        std::thread::id thread;
        std::mutex mutex;
        std::deque<std::function<void()>> pending;
        std::function<void(const std::function<void()>&)> deliveryBoundary;
    };
}
