module;

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

export module Rev.Element.Event.GestureTracker;

import Rev.Element.Event;

export namespace Rev::Element {

    // ------------------------------------------------------------------
    // GestureTracker — matches key sequences and modifier combos.
    //
    // Each keyDown event is converted to a token and appended to a
    // rolling buffer.  When the buffer matches a registered binding,
    // onGesture fires.
    //
    // Token format:
    //   Plain letter          →  "r"
    //   Ctrl + letter         →  "ctrl+r"
    //   Shift + letter        →  "shift+r"
    //   Ctrl + Shift + letter →  "ctrl+shift+r"
    //   Ctrl + Enter          →  "ctrl+enter"
    //   (modifiers are sorted: ctrl → shift → alt → key)
    //
    // Example bindings:
    //   { "rs",       Command::Reset      }   // type r then s
    //   { "ctrl+r",   Command::Reset      }   // Ctrl+R in one step
    //   { "ctrl+cn",  Command::Connect    }   // Ctrl held, type c then n
    //   { "ctrl+rctrl+s", Command::Save   }   // two ctrl-combos in sequence
    //
    // Escape always clears the buffer without consuming the event.
    // ------------------------------------------------------------------

    template<typename Command>
    struct GestureTracker {

        struct Binding {
            std::string sequence;
            Command command;
        };

        using GestureCallback   = std::function<void(Command, Event&)>;
        using SpamDetectCallback = std::function<void(Event&)>;

        std::vector<Binding> bindings;
        GestureCallback      onGesture;
        SpamDetectCallback   onSpamDetect;

        // Milliseconds before an incomplete sequence is discarded.
        uint64_t sequenceTimeoutMs = 1500;

        // After a broken sequence, ignore gesture input for this long.
        uint64_t spamCooldownMs = 600;

        GestureTracker() = default;

        GestureTracker(std::initializer_list<Binding> init) : bindings(init) {}

        // Feed a keyDown event.  Returns true when the key was consumed
        // as part of a matched, in-progress, or cooldown-blocked gesture.
        bool track(Event& e) {

            if (!e.propagate) { return false; }

            const std::string& key = e.keyboard.key;

            if (key.empty()) { return false; }

            // Escape always clears without consuming.
            if (key == "escape") {
                buffer_.clear();
                ignoredUntilMs_ = 0;
                return false;
            }

            // Build the token for this event.
            // Modifier order: ctrl+ → shift+ → alt+ → key.
            std::string token;
            if (e.keyboard.ctrl)  token += "ctrl+";
            if (e.keyboard.shift) token += "shift+";
            if (e.keyboard.alt)   token += "alt+";
            token += key;

            // Without modifiers, only accept plain lowercase letters.
            // With modifiers, accept any key name (enter, space, r, …).
            bool hasModifier = e.keyboard.ctrl || e.keyboard.shift || e.keyboard.alt;
            if (!hasModifier) {
                if (key.size() != 1 || key[0] < 'a' || key[0] > 'z') {
                    return false;
                }
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
            buffer_ += token;

            if (tryMatch(e)) { return true; }

            if (isPrefix(buffer_)) { return true; }

            // Broke an in-progress prefix — cooldown, then retry as fresh start.
            if (!previous.empty() && isPrefix(previous)) {
                triggerSpamDetect(e);
                return true;
            }

            // Orphan token: see if it starts a new sequence on its own.
            buffer_ = token;

            if (tryMatch(e)) { return true; }
            if (isPrefix(buffer_)) { return true; }

            buffer_.clear();
            return false;
        }

    private:

        std::string buffer_;
        uint64_t    lastKeyTimeMs_  = 0;
        uint64_t    ignoredUntilMs_ = 0;

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
