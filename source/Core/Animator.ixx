module;

#include <cstdint>
#include <functional>
#include <deque>

export module Rev.Core.Animator;

import Rev.Core.Dispatcher;
import Rev.Core.Process;

export namespace Rev::Core {

    struct AnimationEvent {
        uint64_t time    = 0;
        uint64_t deltaMs = 0;

        // Call from inside onFrame to override when the *next* frame fires.
        // Does not affect the animator's base tickPeriodMs — just a one-shot
        // nudge.  e.g. e.delayNext(5000) to back off to 5 s for this cycle.
        void delayNext(uint64_t ms) { nextDelayMs_ = ms; }
        void scheduleNext(uint64_t ms) { delayNext(ms); }  // alias

    private:
        friend struct Animator;
        uint64_t nextDelayMs_ = 0;
    };

    struct Animator {

        enum class State {
            Stopped,
            Playing,
            Paused
        };

        Dispatcher<AnimationEvent> dispatcher;

        State state = State::Stopped;
        uint64_t lastTickMs = 0;
        uint64_t tickPeriodMs = 16;

        struct QueuedFrame {
            uint64_t delayMs = 0;
            std::function<void(AnimationEvent&)> callback;
        };

        explicit Animator(uint64_t periodMs = 16) : tickPeriodMs(periodMs) {}

        Animator(const Animator&) = delete;
        Animator& operator=(const Animator&) = delete;

        ~Animator() {
            Process::instance().unschedule(this);
        }

        // Milliseconds between frame callbacks (~60 Hz at 16 ms).
        void setPeriod(uint64_t periodMs) {

            if (periodMs == 0) { return; }

            tickPeriodMs = periodMs;

            if (state == State::Playing) {
                requestTicksFromProcess();
            }
        }

        uint64_t getPeriod() const { return tickPeriodMs; }

        // Target callback rate in frames per second (GlobalTime is ms resolution).
        void setFrequency(double frequencyHz) {

            if (frequencyHz <= 0.0) { return; }

            const uint64_t periodMs = static_cast<uint64_t>(1000.0 / frequencyHz + 0.5);

            setPeriod(std::max<uint64_t>(1, periodMs));
        }

        double getFrequency() const {
            return 1000.0 / static_cast<double>(tickPeriodMs);
        }

        State getState() const { return state; }

        bool isPlaying() const {
            return state == State::Playing;
        }

        void play() {

            if (state == State::Playing) { return; }

            state = State::Playing;
            lastTickMs = 0;

            AnimationEvent event;
            dispatcher.tell(&Animator::dispatchPlay, event);

            requestTicksFromProcess();
        }

        void pause() {

            if (state != State::Playing) { return; }

            state = State::Paused;
            lastTickMs = 0;

            Process::instance().unschedule(this);

            AnimationEvent event;
            dispatcher.tell(&Animator::dispatchPause, event);
        }

        void stop() {

            if (state == State::Stopped) { return; }

            state = State::Stopped;
            lastTickMs = 0;

            Process::instance().unschedule(this);

            AnimationEvent event;
            dispatcher.tell(&Animator::dispatchStop, event);
            dispatcher.tell(&Animator::dispatchEnd, event);
        }

        void onFrame(const std::function<void(AnimationEvent&)>& listener) {
            dispatcher.listen(&Animator::dispatchFrame, listener);
        }

        // Queue a one-shot callback to fire after delayMs.
        // Queued frames fire in order, each with its own delay measured
        // from when the previous one completed.  When the queue is exhausted,
        // normal onFrame callbacks resume at tickPeriodMs.
        //
        //   animator.queueFrame(500,  [](auto& e) { send("G28");  });
        //   animator.queueFrame(2000, [](auto& e) { send("?");    });
        //
        void queueFrame(uint64_t delayMs, std::function<void(AnimationEvent&)> cb) {
            queue_.push_back({ delayMs, std::move(cb) });
            if (!isPlaying()) { play(); }
            else { applyNextDelay(); }  // reschedule for earliest next wake
        }

        // Alias — reads naturally as "schedule this to happen in N ms".
        void schedule(uint64_t delayMs, std::function<void(AnimationEvent&)> cb) {
            queueFrame(delayMs, std::move(cb));
        }

        void onStart(const std::function<void(AnimationEvent&)>& listener) {
            dispatcher.listen(&Animator::dispatchStart, listener);
        }

        void onEnd(const std::function<void(AnimationEvent&)>& listener) {
            dispatcher.listen(&Animator::dispatchEnd, listener);
        }

        void onPlay(const std::function<void(AnimationEvent&)>& listener) {
            dispatcher.listen(&Animator::dispatchPlay, listener);
        }

        void onPause(const std::function<void(AnimationEvent&)>& listener) {
            dispatcher.listen(&Animator::dispatchPause, listener);
        }

        void onStop(const std::function<void(AnimationEvent&)>& listener) {
            dispatcher.listen(&Animator::dispatchStop, listener);
        }

    private:

        std::deque<QueuedFrame> queue_;

        void requestTicksFromProcess() {
            Process::instance().schedule(
                this,
                tickPeriodMs,
                [this](uint64_t now) { tickFromProcess(now); }
            );
        }

        // Point the process schedule at the right next delay:
        // the front queue item's delay if queued, otherwise tickPeriodMs.
        void applyNextDelay() {
            if (queue_.empty()) { return; }
            Process::instance().rescheduleNext(this, queue_.front().delayMs);
        }

        void tickFromProcess(uint64_t now) {

            if (state != State::Playing) { return; }

            const bool starting = (lastTickMs == 0);

            AnimationEvent event;
            event.time = now;

            if (starting) {
                event.deltaMs = 0;
                lastTickMs    = now;
                dispatcher.tell(&Animator::dispatchStart, event);
            } else {
                event.deltaMs = now - lastTickMs;
                lastTickMs    = now;
            }

            if (!queue_.empty()) {

                // -- Queue mode: fire the front item and advance -----------
                QueuedFrame frame = std::move(queue_.front());
                queue_.pop_front();

                frame.callback(event);

                // Schedule next: front of remaining queue, or back to normal
                if (!queue_.empty()) {
                    Process::instance().rescheduleNext(this, queue_.front().delayMs);
                } else if (event.nextDelayMs_ > 0) {
                    Process::instance().rescheduleNext(this, event.nextDelayMs_);
                }
                // else: Process will use its stored intervalMs (tickPeriodMs)

            } else {

                // -- Normal mode: fire onFrame listeners -------------------
                dispatcher.tell(&Animator::dispatchFrame, event);

                // Honour e.delayNext() / e.scheduleNext() if called.
                if (event.nextDelayMs_ > 0) {
                    Process::instance().rescheduleNext(this, event.nextDelayMs_);
                }
            }
        }

        void dispatchFrame(AnimationEvent& event) {}
        void dispatchStart(AnimationEvent& event) {}
        void dispatchEnd(AnimationEvent& event) {}
        void dispatchPlay(AnimationEvent& event) {}
        void dispatchPause(AnimationEvent& event) {}
        void dispatchStop(AnimationEvent& event) {}
    };
}
