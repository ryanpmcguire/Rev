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
import Rev.Core.MainDispatch;

export namespace Rev::Core {

enum class AsyncExecutor { Promise };

// One cooperative scheduler services every blue-stack root. A coroutine owns
// this thread only while making immediate progress; an unresolved co_await
// parks its frame and returns the thread to this scheduler.
class PromiseManager {
public:
    static PromiseManager& instance() { static PromiseManager self; return self; }

    void attachMainThread() { MainDispatch::instance().attach(); }

    void schedule(std::coroutine_handle<> coroutine,
                  AsyncExecutor executor = AsyncExecutor::Promise) {
        if (!coroutine) return;
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
    bool isMainThread() const { return MainDispatch::instance().isMainThread(); }
    static AsyncExecutor currentExecutor() { return executingOn; }
    void setExecutionBoundary(std::function<void(const std::function<void()>&)> boundary) {
        std::lock_guard lock(mutex); executionBoundary = std::move(boundary);
    }

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
            std::function<void(const std::function<void()>&)> boundary;
            {
                std::unique_lock lock(mutex);
                while (!stop.stop_requested()) {
                    const auto now = std::chrono::steady_clock::now();
                    auto deadline = std::chrono::steady_clock::time_point::max();
                    for (auto it = timers.begin(); it != timers.end();) {
                        if (it->due <= now) { due.push_back(*it); it = timers.erase(it); }
                        else { deadline = std::min(deadline, it->due); ++it; }
                    }
                    if (!ready.empty() || !due.empty()) break;
                    if (deadline == std::chrono::steady_clock::time_point::max()) wake.wait(lock);
                    else wake.wait_until(lock, deadline);
                }
                if (!ready.empty()) { next = ready.front(); ready.pop_front(); }
                boundary = executionBoundary;
            }
            for (const auto& timer : due) schedule(timer.coroutine, timer.executor);
            if (next) {
                if (boundary) boundary([next] { resume(next, AsyncExecutor::Promise); });
                else resume(next, AsyncExecutor::Promise);
            }
        }
    }

    static inline thread_local AsyncExecutor executingOn = AsyncExecutor::Promise;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::coroutine_handle<>> ready;
    std::vector<Timer> timers;
    std::function<void(const std::function<void()>&)> executionBoundary;
    // Construct synchronization state before starting the worker. Declaring
    // the thread last also makes it join before that state is destroyed.
    std::jthread worker;
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
template<typename T> struct CoroutineResult {
    std::shared_ptr<PromiseState<T>> state = std::make_shared<PromiseState<T>>();
    template<typename U> void return_value(U&& value) {
        std::lock_guard lock(state->mutex); state->value.emplace(std::forward<U>(value));
    }
};
template<> struct CoroutineResult<void> {
    std::shared_ptr<PromiseState<void>> state = std::make_shared<PromiseState<void>>();
    template<typename Dummy = void> void return_void() const noexcept {}
};
template<typename State, typename Complete>
bool settle(const std::shared_ptr<State>& state, Complete&& complete) {
    std::vector<Continuation> continuations;
    {
        std::lock_guard lock(state->mutex);
        if (state->complete) return false;
        std::forward<Complete>(complete)();
        state->complete = true;
        continuations.swap(state->continuations);
    }
    state->settled.notify_all();
    for (const auto& continuation : continuations)
        PromiseManager::instance().schedule(continuation.coroutine, continuation.executor);
    return true;
}
template<typename State> void finish(const std::shared_ptr<State>& state) {
    settle(state, [] {});
}
// Copyable results remain reusable. Move-only results have one consumer,
// including when multiple Promise handles share the same state.
template<typename T> T consume(PromiseState<T>& state) {
    if constexpr (std::is_copy_constructible_v<T>) {
        return *state.value;
    } else {
        if (!state.value) throw std::logic_error("Promise result already consumed.");
        T result = std::move(*state.value);
        state.value.reset();
        return result;
    }
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
    using Result = Detail::CoroutineResult<T>;
    struct promise_type : Result {
        Promise get_return_object() { return Promise(this->state); }
        Detail::InitialSchedule initial_suspend() const noexcept { return {}; }
        Detail::FinalSchedule<State> final_suspend() const noexcept { return {this->state}; }
        void unhandled_exception() noexcept {
            std::lock_guard lock(this->state->mutex); this->state->error = std::current_exception();
        }
    };

    // Explicitly settles a leaf Promise from an event callback. Copies share
    // one state, and the first resolve/reject operation wins atomically.
    class Source {
    public:
        Source() : state(std::make_shared<State>()) {}
        Promise promise() const { return Promise(state); }
        bool complete() const {
            std::lock_guard lock(state->mutex); return state->complete;
        }
        template<typename U> requires (!std::is_void_v<T>)
        void resolve(U&& value) const {
            Detail::settle(state, [&] {
                state->value.emplace(std::forward<U>(value));
            });
        }
        void resolve() const requires std::is_void_v<T> { Detail::finish(state); }
        void reject(std::exception_ptr error) const {
            Detail::settle(state, [&] {
                state->error = std::move(error);
            });
        }
        template<typename Error> void reject(Error&& error) const {
            reject(std::make_exception_ptr(std::forward<Error>(error)));
        }
    private:
        std::shared_ptr<State> state;
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
    T get() const requires (!std::is_void_v<T>) { wait(); std::lock_guard lock(state->mutex); return Detail::consume(*state); }
    void get() const requires std::is_void_v<T> { wait(); }

    struct Awaiter {
        std::shared_ptr<State> state;
        bool await_ready() const { std::lock_guard lock(state->mutex); return state->complete; }
        bool await_suspend(std::coroutine_handle<> coroutine) {
            std::lock_guard lock(state->mutex); if (state->complete) return false;
            state->continuations.push_back({coroutine, PromiseManager::currentExecutor()}); return true;
        }
        T await_resume() requires (!std::is_void_v<T>) {
            std::lock_guard lock(state->mutex); if (state->error) std::rethrow_exception(state->error); return Detail::consume(*state);
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

// A heterogeneous completion group is represented internally by void adapters;
// the original typed promises continue to own and expose their results.
class PromiseGroup {
public:
    template<typename T> void add(Promise<T> promise) { members.push_back(asVoid(std::move(promise))); }
    bool empty() const noexcept { return members.empty(); }
    std::size_t size() const noexcept { return members.size(); }
    void wait() const { for (const auto& member : members) member.wait(); }
    Promise<void> all() const {
        for (const auto& member : members) co_await member;
    }
private:
    template<typename T> static Promise<void> asVoid(Promise<T> promise) {
        co_await promise;
    }
    std::vector<Promise<void>> members;
};

namespace Async {
struct SwitchExecutor {
    AsyncExecutor target;
    bool await_ready() const noexcept { return PromiseManager::currentExecutor() == target; }
    void await_suspend(std::coroutine_handle<> coroutine) const { PromiseManager::instance().schedule(coroutine, target); }
    void await_resume() const noexcept {}
};
// GUI publication is a discrete callback, never a coroutine executor switch.
template<typename Function> void postMain(Function&& function) {
    MainDispatch::instance().post(std::forward<Function>(function));
}
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

namespace Detail {
// A coroutine lambda's frame refers to its closure object; it does not own a
// second copy of the captures. Keep that closure in this outer coroutine's
// frame until the promise returned by the callable has finished.
template<typename Function>
Promise<void> own(Function function) {
    co_await std::invoke(function);
}
}

template<typename Function> void Do(Function&& function) {
    using OwnedFunction = std::decay_t<Function>;
    [[maybe_unused]] auto root = Detail::own(
        OwnedFunction(std::forward<Function>(function)));
}
}
}
