module;

#include <algorithm>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

export module Rev.Core.RevisionFlag;

export namespace Rev::Core {

    struct RevisionFlag {

        using Revision = uint64_t;

        Revision revision = 0;
        std::vector<RevisionFlag*> targets;
        std::vector<std::function<void()>> callbacks;

        [[nodiscard]] Revision get() const noexcept {
            return revision;
        }

        void setRevision(Revision value) {
            if (value == revision) { return; }

            revision = value;
            callListeners();

            for (RevisionFlag* flag : targets) {
                if (flag) { flag->setRevision(revision); }
            }
        }

        void markDirty() {
            setRevision(revision + 1);
        }

        template <typename Func>
        void onDirty(Func&& func) {
            callbacks.emplace_back(std::forward<Func>(func));
        }

        void callListeners() {
            for (auto& cb : callbacks) {
                if (cb) { cb(); }
            }
        }

        void subscribe(RevisionFlag* source) noexcept {
            if (source && source != this) { source->sendsTo(this); }
        }

        void sendsTo(RevisionFlag* target) noexcept {
            if (!target || target == this) { return; }

            auto it = std::find(targets.begin(), targets.end(), target);

            if (it == targets.end()) {
                targets.push_back(target);
                target->setRevision(revision);
            }
        }

        void disconnect(RevisionFlag* flag) noexcept {
            auto it = std::remove(targets.begin(), targets.end(), flag);
            if (it != targets.end()) { targets.erase(it, targets.end()); }
        }

        operator Revision() const noexcept {
            return revision;
        }
    };

    struct RevisionObserver {

        RevisionFlag::Revision revision = 0;
        bool initialized = false;

        bool changed(const RevisionFlag& flag) {
            RevisionFlag::Revision current = flag.get();

            if (!initialized || current != revision) {
                revision = current;
                initialized = true;
                return true;
            }

            return false;
        }

        void reset() {
            revision = 0;
            initialized = false;
        }
    };
}
