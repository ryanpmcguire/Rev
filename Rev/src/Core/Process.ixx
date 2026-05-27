module;

#include <algorithm>
#include <cstdint>
#include <functional>
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
        void schedule(
            void* owner,
            uint64_t intervalMs,
            TickCallback callback
        ) {

            if (!owner || !callback || intervalMs == 0) { return; }

            unschedule(owner);

            GlobalTime::Update();

            const uint64_t now = GlobalTime::CurrentMs();

            schedules.push_back({
                owner,
                intervalMs,
                now,
                std::move(callback)
            });
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

            const uint64_t now = GlobalTime::CurrentMs();

            std::vector<ScheduledTick> snapshot = schedules;

            for (ScheduledTick& entry : snapshot) {

                if (now < entry.nextWakeMs) { continue; }

                auto live = std::find_if(
                    schedules.begin(),
                    schedules.end(),
                    [&](const ScheduledTick& scheduled) {
                        return scheduled.owner == entry.owner;
                    }
                );

                if (live == schedules.end()) { continue; }

                live->callback(now);

                live->nextWakeMs = now + live->intervalMs;
            }
        }

        bool needsFrame() const {
            return !schedules.empty();
        }

    private:

        struct ScheduledTick {
            void* owner = nullptr;
            uint64_t intervalMs = 0;
            uint64_t nextWakeMs = 0;
            TickCallback callback;
        };

        std::vector<ScheduledTick> schedules;
    };
}
