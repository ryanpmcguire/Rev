module;

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

export module Rev.Core.Guard;

export namespace Rev {

    // A fair access gate for resources whose callers need to distinguish
    // cooperative cohorts from truly exclusive operations. Requests are
    // admitted in FIFO order; adjacent friendly requests of the same kind may
    // enter together, but never pass an earlier incompatible request.
    class Guard {
    public:
        enum class Mode {
            FriendlyRead,
            ExclusiveRead,
            FriendlyWrite,
            ExclusiveWrite
        };

        class Access {
        public:
            Access() = default;
            Access(const Access&) = delete;
            Access& operator=(const Access&) = delete;

            Access(Access&& other) noexcept
                : owner(std::exchange(other.owner, nullptr)) {}

            Access& operator=(Access&& other) noexcept {
                if (this == &other) { return *this; }
                release();
                owner = std::exchange(other.owner, nullptr);
                return *this;
            }

            ~Access() { release(); }

            explicit operator bool() const { return owner != nullptr; }

            void release() {
                if (!owner) { return; }
                Guard* releasing = std::exchange(owner, nullptr);
                releasing->releaseOne();
            }

        private:
            friend class Guard;
            explicit Access(Guard* owner) : owner(owner) {}
            Guard* owner = nullptr;
        };

        Access request(Mode mode) {
            Waiter waiter{ mode };
            std::unique_lock lock(access);
            queue.push_back(&waiter);
            changed.wait(lock, [this, &waiter]() {
                return canAdmit(&waiter);
            });

            queue.pop_front();
            if (activeCount == 0) { activeMode = mode; }
            ++activeCount;
            changed.notify_all();
            return Access(this);
        }

        size_t waiting() const {
            std::scoped_lock lock(access);
            return queue.size();
        }

        size_t active() const {
            std::scoped_lock lock(access);
            return activeCount;
        }

    private:
        struct Waiter {
            Mode mode;
        };

        mutable std::mutex access;
        std::condition_variable changed;
        std::deque<Waiter*> queue;
        Mode activeMode = Mode::ExclusiveWrite;
        size_t activeCount = 0;

        static bool friendly(Mode mode) {
            return mode == Mode::FriendlyRead || mode == Mode::FriendlyWrite;
        }

        bool canAdmit(const Waiter* waiter) const {
            if (queue.empty() || queue.front() != waiter) { return false; }
            if (activeCount == 0) { return true; }
            return friendly(waiter->mode) && activeMode == waiter->mode;
        }

        void releaseOne() {
            std::scoped_lock lock(access);
            if (activeCount > 0) { --activeCount; }
            changed.notify_all();
        }
    };
}
