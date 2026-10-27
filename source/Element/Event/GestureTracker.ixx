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
    // Escape clears and consumes an active gesture. With no gesture active it
    // remains available to the rest of the application.
    // ------------------------------------------------------------------

    template<typename Command>
    struct GestureTracker {

        struct Binding {
            std::string sequence;
            Command command;
        };

        using GestureCallback   = std::function<void(Command, Event&)>;
        using SpamDetectCallback = std::function<void(Event&)>;
        using StateChangeCallback = std::function<void()>;

        std::vector<Binding> bindings;
        GestureCallback      onGesture;
        SpamDetectCallback   onSpamDetect;
        StateChangeCallback  onStateChange;

        // When enabled, an exact sequence remains pending until its final
        // letter is pressed once more. For example, "sfz" activates on
        // "sfzz". This gives the UI a chance to present the exact match and
        // its remaining alternatives before committing it.
        bool requireFinalKeyConfirmation = false;

        // Milliseconds before an incomplete sequence is discarded.
        uint64_t sequenceTimeoutMs = 1500;

        // After a broken sequence, ignore gesture input for this long.
        uint64_t spamCooldownMs = 600;

        GestureTracker() = default;

        GestureTracker(std::initializer_list<Binding> init) : bindings(init) {}

        const std::string& entered() const { return buffer_; }
        bool awaitingConfirmation() const {
            return pendingBinding_ < bindings.size();
        }
        std::string confirmationKey() const {
            if (!awaitingConfirmation() || bindings[pendingBinding_].sequence.empty()) {
                return {};
            }
            return std::string(1, bindings[pendingBinding_].sequence.back());
        }
        std::vector<Binding> possibleMatches() const {
            std::vector<Binding> matches;
            if (buffer_.empty()) { return matches; }
            for (const Binding& binding : bindings) {
                if (binding.sequence.starts_with(buffer_)) {
                    matches.push_back(binding);
                }
            }
            return matches;
        }
        void clear() {
            const bool changed = !buffer_.empty() || awaitingConfirmation();
            buffer_.clear();
            pendingBinding_ = noBinding;
            ignoredUntilMs_ = 0;
            if (changed) { notifyStateChange(); }
        }

        // Feed a keyDown event.  Returns true when the key was consumed
        // as part of a matched, in-progress, or cooldown-blocked gesture.
        bool track(Event& e) {

            if (!e.propagate) { return false; }

            const std::string& key = e.keyboard.key;

            if (key.empty()) { return false; }

            // Escape belongs to the tracker only while an attempt is active.
            if (key == "escape") {
                const bool wasTracking = !buffer_.empty() ||
                    awaitingConfirmation() || ignoredUntilMs_ != 0;
                clear();
                return wasTracking;
            }

            // Build the token for this event.
            // Modifier order: ctrl+ → shift+ → alt+ → key.
            std::string token;
            if (e.keyboard.ctrl)  token += "ctrl+";
            if (e.keyboard.shift) token += "shift+";
            if (e.keyboard.alt)   token += "alt+";
            token += key;

            // Without modifiers, accept plain lowercase letters and digits.
            // With modifiers, accept any key name (enter, space, r, …).
            bool hasModifier = e.keyboard.ctrl || e.keyboard.shift || e.keyboard.alt;
            if (!hasModifier) {
                if (key.size() != 1 || !(
                        (key[0] >= 'a' && key[0] <= 'z') ||
                        (key[0] >= '0' && key[0] <= '9')
                    )) {
                    return false;
                }
            }

            if (ignoredUntilMs_ && e.time < ignoredUntilMs_) {
                return true;
            }

            ignoredUntilMs_ = 0;

            if (!buffer_.empty() && e.time - lastKeyTimeMs_ > sequenceTimeoutMs) {
                buffer_.clear();
                pendingBinding_ = noBinding;
                notifyStateChange();
            }

            if (awaitingConfirmation()) {
                const size_t bindingIndex = pendingBinding_;
                if (!hasModifier && key == confirmationKey()) {
                    activate(bindingIndex, e);
                    return true;
                }
                // An exact binding can also be the prefix of a longer one.
                // Continue into that longer candidate instead of forcing the
                // shorter command to activate first ("sfz" vs "sfz8").
                if (isPrefix(buffer_ + token)) {
                    pendingBinding_ = noBinding;
                    notifyStateChange();
                }
                else {
                    buffer_.clear();
                    pendingBinding_ = noBinding;
                    notifyStateChange();
                }
            }

            std::string previous = buffer_;

            lastKeyTimeMs_ = e.time;
            buffer_ += token;
            notifyStateChange();

            if (tryMatch(e)) { return true; }

            if (isPrefix(buffer_)) { return true; }

            // Broke an in-progress prefix — cooldown, then retry as fresh start.
            if (!previous.empty() && isPrefix(previous)) {
                triggerSpamDetect(e);
                return true;
            }

            // Orphan token: see if it starts a new sequence on its own.
            buffer_ = token;
            notifyStateChange();

            if (tryMatch(e)) { return true; }
            if (isPrefix(buffer_)) { return true; }

            buffer_.clear();
            notifyStateChange();
            return false;
        }

    private:

        std::string buffer_;
        uint64_t    lastKeyTimeMs_  = 0;
        uint64_t    ignoredUntilMs_ = 0;
        static constexpr size_t noBinding = static_cast<size_t>(-1);
        size_t pendingBinding_ = noBinding;

        bool isPrefix(const std::string& sequence) const {
            for (const Binding& binding : bindings) {
                if (binding.sequence.starts_with(sequence)) {
                    return true;
                }
            }
            return false;
        }

        bool tryMatch(Event& e) {
            for (size_t index = 0; index < bindings.size(); ++index) {
                const Binding& binding = bindings[index];
                if (binding.sequence != buffer_) { continue; }
                if (requireFinalKeyConfirmation) {
                    pendingBinding_ = index;
                    notifyStateChange();
                    return true;
                }
                activate(index, e);
                return true;
            }
            return false;
        }

        void triggerSpamDetect(Event& e) {
            buffer_.clear();
            pendingBinding_ = noBinding;
            ignoredUntilMs_ = e.time + spamCooldownMs;
            notifyStateChange();

            if (onSpamDetect) {
                onSpamDetect(e);
            }
        }

        void activate(size_t index, Event& e) {
            const Command command = bindings[index].command;
            buffer_.clear();
            pendingBinding_ = noBinding;
            notifyStateChange();
            if (onGesture) { onGesture(command, e); }
        }

        void notifyStateChange() {
            if (onStateChange) { onStateChange(); }
        }
    };
}
