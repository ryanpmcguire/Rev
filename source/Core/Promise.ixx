module;
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <concepts>
#include <coroutine>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

export module Rev.Core.Promise;
import Rev.Core.Process;

export namespace Rev::Core {

enum class AsyncExecutor { Promise, Main };

// One cooperative scheduler services every blue-stack root. A coroutine owns
// this thread only while making immediate progress; an unresolved co_await
// parks its frame and returns the thread to this scheduler.
class PromiseManager {
public:
    static PromiseManager& instance() { static PromiseManager self; return self; }

    void attachMainThread() {
        bool expected = false;
        if (!mainAttached.compare_exchange_strong(expected, true)) return;
        mainThread = std::this_thread::get_id();
        Process::instance().schedule(this, MainPumpIntervalMs,
            [this](uint64_t) { pumpMain(); });
    }

    void schedule(std::coroutine_handle<> coroutine,
                  AsyncExecutor executor = AsyncExecutor::Promise) {
        if (!coroutine) return;
        if (executor == AsyncExecutor::Main) {
            std::lock_guard lock(mainMutex); mainReady.push_back(coroutine); return;
        }
        { std::lock_guard lock(mutex); ready.push_back(coroutine); }
        wake.notify_one();
    }

    void scheduleAfter(std::chrono::steady_clock::duration delay,
                       std::coroutine_handle<> coroutine, AsyncExecutor executor) {
        { std::lock_guard lock(mutex);
          timers.push_back({std::chrono::steady_clock::now() + delay, coroutine, executor}); }
        wake.notify_one();
    }

    bool isPromiseThread() const { return std::this_thread::get_id() == worker.get_id(); }
    bool isMainThread() const { return mainAttached && std::this_thread::get_id() == mainThread; }
    static AsyncExecutor currentExecutor() { return executingOn; }

private:
    struct Timer { std::chrono::steady_clock::time_point due; std::coroutine_handle<> coroutine; AsyncExecutor executor; };
    PromiseManager() : worker([this](std::stop_token stop) { run(stop); }) {}
    ~PromiseManager() { worker.request_stop(); wake.notify_all(); }

    static void resume(std::coroutine_handle<> coroutine, AsyncExecutor executor) {
        const auto previous = executingOn; executingOn = executor;
        coroutine.resume();
        if (coroutine.done()) coroutine.destroy();
        executingOn = previous;
    }

    void run(std::stop_token stop) {
        while (!stop.stop_requested()) {
            std::coroutine_handle<> next;
            std::vector<Timer> due;
            {
                std::unique_lock lock(mutex);
                while (!stop.stop_requested() && ready.empty()) {
                    auto deadline = std::chrono::steady_clock::time_point::max();
                    for (const auto& timer : timers) deadline = std::min(deadline, timer.due);
                    if (deadline == std::chrono::steady_clock::time_point::max()) wake.wait(lock);
                    else wake.wait_until(lock, deadline);
                    const auto now = std::chrono::steady_clock::now();
                    for (auto it = timers.begin(); it != timers.end();)
                        if (it->due <= now) { due.push_back(*it); it = timers.erase(it); }
                        else ++it;
                    if (!due.empty()) break;
                }
                if (!ready.empty()) { next = ready.front(); ready.pop_front(); }
            }
            for (const auto& timer : due) schedule(timer.coroutine, timer.executor);
            if (next) resume(next, AsyncExecutor::Promise);
        }
    }

    void pumpMain() {
        std::deque<std::coroutine_handle<>> local;
        { std::lock_guard lock(mainMutex); local.swap(mainReady); }
        while (!local.empty()) { auto item = local.front(); local.pop_front(); resume(item, AsyncExecutor::Main); }
    }

    static inline thread_local AsyncExecutor executingOn = AsyncExecutor::Promise;
    static constexpr uint64_t MainPumpIntervalMs = 8;
    std::jthread worker;
    std::mutex mutex, mainMutex;
    std::condition_variable wake;
    std::deque<std::coroutine_handle<>> ready, mainReady;
    std::vector<Timer> timers;
    std::atomic<bool> mainAttached = false;
    std::thread::id mainThread;
};

namespace Detail {
struct Continuation { std::coroutine_handle<> coroutine; AsyncExecutor executor; };
template<typename T> struct PromiseState {
    mutable std::mutex mutex; std::condition_variable settled; bool complete = false;
    std::exception_ptr error; std::optional<T> value; std::vector<Continuation> continuations;
};
template<> struct PromiseState<void> {
    mutable std::mutex mutex; std::condition_variable settled; bool complete = false;
    std::exception_ptr error; std::vector<Continuation> continuations;
};
template<typename State> void finish(const std::shared_ptr<State>& state) {
    std::vector<Continuation> continuations;
    { std::lock_guard lock(state->mutex); state->complete = true; continuations.swap(state->continuations); }
    state->settled.notify_all();
    for (const auto& continuation : continuations)
        PromiseManager::instance().schedule(continuation.coroutine, continuation.executor);
}
struct InitialSchedule {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> coroutine) const { PromiseManager::instance().schedule(coroutine); }
    void await_resume() const noexcept {}
};
template<typename State> struct FinalSchedule {
    std::shared_ptr<State> state;
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) const noexcept { finish(state); }
    void await_resume() const noexcept {}
};
}

template<typename T = void>
class Promise {
public:
    using State = Detail::PromiseState<T>;
    struct promise_type {
        std::shared_ptr<State> state = std::make_shared<State>();
        Promise get_return_object() { return Promise(state); }
        Detail::InitialSchedule initial_suspend() const noexcept { return {}; }
        Detail::FinalSchedule<State> final_suspend() const noexcept { return {state}; }
        void unhandled_exception() noexcept { std::lock_guard lock(state->mutex); state->error = std::current_exception(); }
        template<typename U> requires (!std::is_void_v<T>)
        void return_value(U&& value) { std::lock_guard lock(state->mutex); state->value.emplace(std::forward<U>(value)); }
        void return_void() requires std::is_void_v<T> {}
    };

    Promise() = default;
    bool valid() const noexcept { return static_cast<bool>(state); }
    bool ready() const { if (!state) return false; std::lock_guard lock(state->mutex); return state->complete; }

    // Cancellation will become a stop-token operation. Kept as a source-level
    // bridge while existing Rev clients migrate to owned Async scopes.
    void kill() noexcept {}

    void wait() const {
        requireState(); auto& manager = PromiseManager::instance();
        if (manager.isPromiseThread() || manager.isMainThread())
            throw std::logic_error("Promise::wait() cannot block an async executor; use co_await.");
        std::unique_lock lock(state->mutex);
        state->settled.wait(lock, [this] { return state->complete; });
        if (state->error) std::rethrow_exception(state->error);
    }
    T get() const requires (!std::is_void_v<T>) { wait(); std::lock_guard lock(state->mutex); return *state->value; }
    void get() const requires std::is_void_v<T> { wait(); }

    struct Awaiter {
        std::shared_ptr<State> state;
        bool await_ready() const { std::lock_guard lock(state->mutex); return state->complete; }
        bool await_suspend(std::coroutine_handle<> coroutine) {
            std::lock_guard lock(state->mutex); if (state->complete) return false;
            state->continuations.push_back({coroutine, PromiseManager::currentExecutor()}); return true;
        }
        T await_resume() requires (!std::is_void_v<T>) {
            std::lock_guard lock(state->mutex); if (state->error) std::rethrow_exception(state->error); return *state->value;
        }
        void await_resume() requires std::is_void_v<T> {
            std::lock_guard lock(state->mutex); if (state->error) std::rethrow_exception(state->error);
        }
    };
    Awaiter operator co_await() const { requireState(); return {state}; }

private:
    explicit Promise(std::shared_ptr<State> value) : state(std::move(value)) {}
    void requireState() const { if (!state) throw std::logic_error("Operation requires a valid Promise."); }
    std::shared_ptr<State> state;
};

namespace Async {
struct SwitchExecutor {
    AsyncExecutor target;
    bool await_ready() const noexcept { return PromiseManager::currentExecutor() == target; }
    void await_suspend(std::coroutine_handle<> coroutine) const { PromiseManager::instance().schedule(coroutine, target); }
    void await_resume() const noexcept {}
};
inline SwitchExecutor mainThread() { return {AsyncExecutor::Main}; }
inline SwitchExecutor promiseThread() { return {AsyncExecutor::Promise}; }

struct Delay {
    std::chrono::steady_clock::duration duration;
    bool await_ready() const noexcept { return duration <= duration.zero(); }
    void await_suspend(std::coroutine_handle<> coroutine) const {
        PromiseManager::instance().scheduleAfter(duration, coroutine, PromiseManager::currentExecutor());
    }
    void await_resume() const noexcept {}
};
template<typename Rep, typename Period> Delay delay(std::chrono::duration<Rep, Period> duration) {
    return {std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration)};
}

template<typename Function> void Do(Function&& function) {
    PromiseManager::instance().attachMainThread();
    [[maybe_unused]] auto root = std::invoke(std::forward<Function>(function));
}
}
}
