module;
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#endif

export module Rev.Core.MainDispatch;
import Rev.Core.Process;

export namespace Rev::Core {
    // A mailbox for observations/rendering only. It never owns coroutine
    // handles and cannot migrate Promise execution onto the GUI thread.
    class MainDispatch {
    public:
#ifdef _WIN32
        static constexpr unsigned WakeMessage = WM_APP + 72;
#endif
        static MainDispatch& instance() { static MainDispatch self; return self; }
        void attach() {
            if (attached.load()) return;
            thread = std::this_thread::get_id();
#ifdef _WIN32
            // Create this thread's Win32 message queue before worker threads can
            // attempt to wake it with PostThreadMessageW.
            MSG message{};
            PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
            nativeThread = GetCurrentThreadId();
#endif
            attached = true;
#ifndef _WIN32
            Process::instance().schedule(this, 8, [this](uint64_t) { drain(); });
#endif
        }
        bool isMainThread() const { return attached && std::this_thread::get_id() == thread; }
        void setDeliveryBoundary(std::function<void(const std::function<void()>&)> boundary) {
            deliveryBoundary = std::move(boundary);
        }
        void post(std::function<void()> callback) {
            {
                std::lock_guard lock(mutex);
                pending.push_back(std::move(callback));
            }
#ifdef _WIN32
            // Wake the GUI event loop only when there is actual work. The old
            // permanent 8 ms Process tick woke an otherwise idle application
            // 125 times per second.
            if (attached.load()) {
                PostThreadMessageW(nativeThread, WakeMessage, 0, 0);
            }
#endif
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
#ifdef _WIN32
        DWORD nativeThread = 0;
#endif
        std::atomic<bool> attached = false;
        std::thread::id thread;
        std::mutex mutex;
        std::deque<std::function<void()>> pending;
        std::function<void(const std::function<void()>&)> deliveryBoundary;
    };
}
