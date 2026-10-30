module;
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

export module Rev.Core.Guard;
import Rev.Core.Promise;

export namespace Rev {
    // Both APIs share FIFO admission. FriendlyWrite permits concurrent writers;
    // use ExclusiveWrite for ordinary mutable shared state.
    class Guard {
        struct State;
    public:
        enum class Mode { FriendlyRead, ExclusiveRead, FriendlyWrite, ExclusiveWrite };
        class Access {
        public:
            Access() = default;
            Access(const Access&) = delete;
            Access& operator=(const Access&) = delete;
            Access(Access&& other) noexcept : owner(std::move(other.owner)) {}
            Access& operator=(Access&& other) noexcept {
                if (this != &other) { release(); owner = std::move(other.owner); }
                return *this;
            }
            ~Access() { release(); }
            explicit operator bool() const { return bool(owner); }
            void release() {
                if (auto shared = std::exchange(owner, {})) Guard::releaseOne(shared);
            }
        private:
            friend class Guard;
            explicit Access(std::shared_ptr<State> shared) : owner(std::move(shared)) {}
            std::shared_ptr<State> owner;
        };

        Guard() = default;
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
        ~Guard() {
            std::deque<std::shared_ptr<Waiter>> pending;
            {
                std::lock_guard lock(state->mutex);
                state->closed = true;
                pending.swap(state->queue);
            }
            state->changed.notify_all();
            for (auto& waiter : pending)
                if (waiter->completion) waiter->completion->reject(
                    std::runtime_error("Guard destroyed while waiting for access."));
        }

        // Blocks, never spins. Use wait() on the cooperative Promise executor.
        Access request(Mode mode) {
            auto shared = state;
            auto waiter = std::make_shared<Waiter>(mode);
            enqueue(shared, waiter);
            std::unique_lock lock(shared->mutex);
            shared->changed.wait(lock, [&] { return waiter->admitted || shared->closed; });
            if (!waiter->admitted) throw std::runtime_error("Guard destroyed while waiting for access.");
            return Access(shared);
        }

        // Registration is synchronous so call order defines FIFO order.
        // Awaiting retains the caller's executor. Release access before awaiting
        // another operation that needs the same incompatible access.
        Core::Promise<Access> wait(Mode mode) {
            auto waiter = std::make_shared<Waiter>(mode);
            waiter->completion.emplace();
            auto promise = waiter->completion->promise();
            enqueue(state, waiter);
            return promise;
        }
        size_t waiting() const { std::lock_guard lock(state->mutex); return state->queue.size(); }
        size_t active() const { std::lock_guard lock(state->mutex); return state->activeCount; }

    private:
        struct Waiter {
            explicit Waiter(Mode mode) : mode(mode) {}
            Mode mode;
            bool admitted = false;
            std::optional<Core::Promise<Access>::Source> completion;
        };
        struct State {
            std::mutex mutex;
            std::condition_variable changed;
            std::deque<std::shared_ptr<Waiter>> queue;
            Mode activeMode = Mode::ExclusiveWrite;
            size_t activeCount = 0;
            bool closed = false;
        };
        std::shared_ptr<State> state = std::make_shared<State>();
        static bool friendly(Mode mode) {
            return mode == Mode::FriendlyRead || mode == Mode::FriendlyWrite;
        }
        // Reserve under the mutex, settle outside it. Destroying an unconsumed
        // Promise result can itself release an Access token.
        static void admit(const std::shared_ptr<State>& shared) {
            std::vector<std::shared_ptr<Waiter>> granted;
            {
                std::lock_guard lock(shared->mutex);
                while (!shared->closed && !shared->queue.empty()) {
                    auto waiter = shared->queue.front();
                    if (shared->activeCount &&
                        !(friendly(waiter->mode) && shared->activeMode == waiter->mode)) break;
                    granted.push_back(waiter);
                    shared->queue.pop_front();
                    shared->activeMode = waiter->mode;
                    ++shared->activeCount;
                    waiter->admitted = true;
                }
            }
            shared->changed.notify_all();
            for (auto& waiter : granted)
                if (waiter->completion) waiter->completion->resolve(Access(shared));
        }
        static void enqueue(const std::shared_ptr<State>& shared, const std::shared_ptr<Waiter>& waiter) {
            {
                std::lock_guard lock(shared->mutex);
                if (shared->closed) throw std::runtime_error("Guard is closed.");
                shared->queue.push_back(waiter);
            }
            admit(shared);
        }
        static void releaseOne(const std::shared_ptr<State>& shared) {
            {
                std::lock_guard lock(shared->mutex);
                --shared->activeCount;
            }
            admit(shared);
        }
    };
}
