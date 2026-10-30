module;
#include <optional>
#include <stdexcept>
#include <functional>

export module Rev.Core.ModelAccess;
import Rev.Core.Guard;
import Rev.Core.Promise;
import Rev.Core.MainDispatch;

export namespace Rev::Core {
    // Explicit, short model access scopes. Never hold a scope over I/O or
    // co_await, and never lock an entire Promise execution slice.
    class ModelAccess {
        static Rev::Guard& guard() { static Rev::Guard value; return value; }
        static inline thread_local unsigned depth = 0;
        static inline thread_local Rev::Guard::Mode heldMode = Rev::Guard::Mode::FriendlyRead;
    public:
        class Scope {
            std::optional<Rev::Guard::Access> access;
        public:
            explicit Scope(Rev::Guard::Mode mode) {
                if (depth && heldMode != Rev::Guard::Mode::ExclusiveWrite
                    && mode != Rev::Guard::Mode::FriendlyRead && mode != heldMode)
                    throw std::logic_error("Cannot upgrade model access; release the outer read scope first.");
                if (!depth) {
                    access.emplace(guard().request(mode));
                    heldMode = mode;
                }
                ++depth;
            }
            Scope(const Scope&) = delete;
            ~Scope() { --depth; }
        };
        static Scope read() { return Scope(Rev::Guard::Mode::FriendlyRead); }
        static Scope serialize() { return Scope(Rev::Guard::Mode::ExclusiveRead); }
        static Scope write() { return Scope(Rev::Guard::Mode::ExclusiveWrite); }
        static void initialize() {
            MainDispatch::instance().attach();
            MainDispatch::instance().setDeliveryBoundary([](const std::function<void()>& update) {
                auto access = read(); update();
            });
        }
    };
}
