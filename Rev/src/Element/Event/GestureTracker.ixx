module;

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

export module Rev.Element.Event.GestureTracker;

import Rev.Element.Event;

export namespace Rev::Element {

    // Builds key sequences like "df" from successive keyDown events and
    // fires a callback when a registered gesture is recognized.
    template<typename Command>
    struct GestureTracker {

        struct Binding {
            std::string sequence;
            Command command;
        };

        using GestureCallback = std::function<void(Command, Event&)>;
        using SpamDetectCallback = std::function<void(Event&)>;

        std::vector<Binding> bindings;
        GestureCallback onGesture;
        SpamDetectCallback onSpamDetect;

        // Milliseconds before an incomplete sequence is discarded.
        uint64_t sequenceTimeoutMs = 1500;

        // After a broken sequence, ignore gesture input for this long.
        uint64_t spamCooldownMs = 600;

        GestureTracker() = default;

        GestureTracker(std::initializer_list<Binding> init) : bindings(init) {}

        // Feed a keyDown event. Returns true when the key was consumed as part
        // of a matched, in-progress, or cooldown-blocked gesture.
        bool track(Event& e) {

            if (!e.propagate) { return false; }
            if (e.keyboard.ctrl || e.keyboard.alt) { return false; }

            const std::string& key = e.keyboard.key;

            if (key.empty()) { return false; }

            if (key == "escape") {
                buffer_.clear();
                ignoredUntilMs_ = 0;
                return false;
            }

            // Gestures are lowercase letter sequences only.
            if (key.size() != 1 || key[0] < 'a' || key[0] > 'z') {
                return false;
            }

            if (ignoredUntilMs_ && e.time < ignoredUntilMs_) {
                return true;
            }

            ignoredUntilMs_ = 0;

            if (!buffer_.empty() && e.time - lastKeyTimeMs_ > sequenceTimeoutMs) {
                buffer_.clear();
            }

            std::string previous = buffer_;

            lastKeyTimeMs_ = e.time;
            buffer_ += key;

            if (tryMatch(e)) { return true; }

            if (isPrefix(buffer_)) { return true; }

            // Broke an in-progress prefix (e.g. d -> x when only df is valid).
            if (!previous.empty() && isPrefix(previous)) {
                triggerSpamDetect(e);
                return true;
            }

            // Orphan key: see if it starts a new sequence on its own.
            buffer_ = key;

            if (tryMatch(e)) { return true; }
            if (isPrefix(buffer_)) { return true; }

            buffer_.clear();
            return false;
        }

    private:

        std::string buffer_;
        uint64_t lastKeyTimeMs_ = 0;
        uint64_t ignoredUntilMs_ = 0;

        bool isPrefix(const std::string& sequence) const {

            for (const Binding& binding : bindings) {
                if (binding.sequence.starts_with(sequence)) {
                    return true;
                }
            }

            return false;
        }

        bool tryMatch(Event& e) {

            for (const Binding& binding : bindings) {
                if (binding.sequence != buffer_) { continue; }

                buffer_.clear();

                if (onGesture) {
                    onGesture(binding.command, e);
                }

                return true;
            }

            return false;
        }

        void triggerSpamDetect(Event& e) {

            buffer_.clear();
            ignoredUntilMs_ = e.time + spamCooldownMs;

            if (onSpamDetect) {
                onSpamDetect(e);
            }
        }
    };
}
