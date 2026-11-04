module;

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

export module Rev.Core.Process;

import Rev.GlobalTime;

export namespace Rev::Core {

    struct Process {

        using TickCallback = std::function<void(uint64_t timeMs)>;

        static Process& instance() {
            static Process self;
            return self;
        }

        // Ask Process to invoke callback every intervalMs while scheduled.
        // Replaces any existing schedule for the same owner.
        //
        // aligned = true puts the owner on the shared frame clock: its wakes
        // land on the global frame grid (frameRateHz, 60 by default), its
        // interval is rounded up to whole frames (so it can never exceed the
        // frame rate), and every aligned owner due on the same frame runs in
        // the same tick() call, so their dirty flags coalesce into one redraw.
        // aligned = false keeps the old free-running behaviour.
        void schedule(
            void* owner,
            uint64_t intervalMs,
            TickCallback callback,
            bool aligned = false
        ) {

            if (!owner || !callback || intervalMs == 0) { return; }

            unschedule(owner);

            GlobalTime::Update();

            const uint64_t now = GlobalTime::now;

            schedules.push_back({
                owner,
                intervalMs,
                aligned ? frameAtOrAfter(now) : now,
                std::move(callback),
                aligned
            });
        }

        // Shared frame clock rate for aligned schedules (display rate).
        void setFrameRate(uint64_t hz) { if (hz > 0) { frameRateHz = hz; } }
        uint64_t getFrameRate() const { return frameRateHz; }

        // First frame-grid time >= t (grid anchored at time 0, fractional
        // period 1000/frameRateHz ms, rounded up to whole ms).
        uint64_t frameAtOrAfter(uint64_t t) const {
            const uint64_t index = (t * frameRateHz + 999) / 1000;
            return (index * 1000 + frameRateHz - 1) / frameRateHz;
        }

        // Override the next wake time for an already-scheduled owner,
        // without changing its repeating intervalMs.
        // Useful for one-shot delay adjustments from within a callback.
        void rescheduleNext(void* owner, uint64_t delayMs) {

            if (!owner) { return; }

            auto it = std::find_if(
                schedules.begin(), schedules.end(),
                [owner](const ScheduledTick& e) { return e.owner == owner; }
            );

            if (it == schedules.end()) { return; }

            GlobalTime::Update();
            it->nextWakeMs = GlobalTime::now + delayMs;
        }

        void unschedule(void* owner) {

            if (!owner) { return; }

            schedules.erase(
                std::remove_if(
                    schedules.begin(),
                    schedules.end(),
                    [owner](const ScheduledTick& entry) {
                        return entry.owner == owner;
                    }
                ),
                schedules.end()
            );
        }

        bool isScheduled(void* owner) const {

            if (!owner) { return false; }

            return std::any_of(
                schedules.begin(),
                schedules.end(),
                [owner](const ScheduledTick& entry) {
                    return entry.owner == owner;
                }
            );
        }

        void tick() {

            GlobalTime::Update();

            const uint64_t now = GlobalTime::now;

            std::vector<void*> dueOwners;

            for (const ScheduledTick& entry : schedules) {
                if (now >= entry.nextWakeMs) {
                    dueOwners.push_back(entry.owner);
                }
            }

            for (void* owner : dueOwners) {

                auto live = std::find_if(
                    schedules.begin(),
                    schedules.end(),
                    [owner](const ScheduledTick& scheduled) {
                        return scheduled.owner == owner;
                    }
                );

                if (live == schedules.end()) { continue; }

                const uint64_t intervalMs = live->intervalMs;
                const TickCallback callback = live->callback;

                callback(now);

                live = std::find_if(
                    schedules.begin(),
                    schedules.end(),
                    [owner](const ScheduledTick& scheduled) {
                        return scheduled.owner == owner;
                    }
                );

                if (live == schedules.end()) { continue; }

                if (live->aligned) {
                    // Snap to the grid. Subtract half a frame so a late wake
                    // does not skip a frame; never schedule at or before now.
                    const uint64_t halfFrame = 500 / frameRateHz;
                    const uint64_t target = now + intervalMs;
                    uint64_t wake = frameAtOrAfter(target > halfFrame ? target - halfFrame : 0);
                    if (wake <= now) { wake = frameAtOrAfter(now + 1); }
                    live->nextWakeMs = wake;
                } else {
                    live->nextWakeMs = now + intervalMs;
                }
            }
        }

        bool hasScheduledTicks() const {
            return !schedules.empty();
        }

        // Milliseconds until the next scheduled tick (0 if due now).
        uint64_t msUntilNextTick() const {

            GlobalTime::Update();

            const uint64_t now = GlobalTime::now;

            uint64_t waitMs = std::numeric_limits<uint64_t>::max();

            for (const ScheduledTick& entry : schedules) {
                if (now >= entry.nextWakeMs) { return 0; }

                const uint64_t remaining = entry.nextWakeMs - now;
                waitMs = std::min(waitMs, remaining);
            }

            if (waitMs == std::numeric_limits<uint64_t>::max()) {
                return 0;
            }

            return waitMs;
        }

    private:

        struct ScheduledTick {
            void* owner = nullptr;
            uint64_t intervalMs = 0;
            uint64_t nextWakeMs = 0;
            TickCallback callback;
            bool aligned = false;
        };

        std::vector<ScheduledTick> schedules;
        uint64_t frameRateHz = 60;
    };
}
