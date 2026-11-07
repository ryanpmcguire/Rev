module;

#include <cstdint>
#include "WinEvent.hpp"

export module Rev.Modal;

import Rev.Window;
import Rev.NativeWindow;
import Rev.Core.Promise;

export namespace Rev {

    // A Window whose whole life is one Promise<Result>.
    //
    //   Result r = co_await (new MyModal(owner, ...))->result();
    //
    // Ownership: create with `new` and never delete. The Window(Window* owner)
    // constructor joins the owner's window group; Application::run deletes
    // every grouped window once shouldClose is set (Application.win.ixx:102).
    //
    // Resolves exactly once (Promise::Source is first-wins):
    //   settle(value)          -> value, then closes.
    //   X / Alt-F4 / OS close  -> ~Modal resolves `dismissed`.
    //   owner torn down first  -> Win32 destroys owned windows before their
    //                             owner, so onEvent(Destroy) arrives while this
    //                             Modal is alive: resolve `dismissed`, drop the
    //                             owner without touching it, close.
    template<typename Result>
    struct Modal : public Window {

        // The owner is disabled for the modal's whole life and centred upon.
        Modal(Window* owner, Details details, Result dismissed)
            : Window(owner, details), owner(owner), dismissed(dismissed) {

            if (owner) {
                const int width = static_cast<int>(this->details.size.width * window->scale);
                const int height = static_cast<int>(this->details.size.height * window->scale);
                const int ownerWidth = static_cast<int>(owner->details.size.width * owner->window->scale);
                const int ownerHeight = static_cast<int>(owner->details.size.height * owner->window->scale);

                setPos(
                    owner->details.x + (ownerWidth - width) / 2,
                    owner->details.y + (ownerHeight - height) / 2
                );

                owner->window->setEnabled(false);
            }
        }

        // The owner is re-enabled here and nowhere else; the promise settles
        // to `dismissed` unless something already settled it.
        ~Modal() {

            if (owner) {
                owner->window->setEnabled(true);
            }

            source.resolve(dismissed);
        }

        // Shows the modal; the promise settles exactly once.
        Core::Promise<Result> result() {
            show();
            return source.promise();
        }

    protected:

        // Subclasses call this from their buttons' Dispatcher callbacks.
        void settle(Result value) {
            source.resolve(value);
            shouldClose = true;
        }

        // A Destroy that reaches this override came while the Modal is alive,
        // so it was not our own ~Window: the owner is being torn down.
        void onEvent(WinEvent& event) override {

            Window::onEvent(event);

            if (event.type == WinEvent::Destroy) {
                owner = nullptr;
                source.resolve(dismissed);
                shouldClose = true;
            }
        }

    private:
        Window* owner = nullptr;
        Result dismissed;
        typename Core::Promise<Result>::Source source;
    };
}
