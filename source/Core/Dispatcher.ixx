module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <type_traits>

export module Rev.Core.Dispatcher;
import Rev.Core.MainDispatch;

export namespace Rev::Core {

    template<typename EventType>
    struct Dispatcher {

        static constexpr size_t ListenerKeySize = 32;
        using ListenerKey = std::array<std::byte, ListenerKeySize>;

        // One subscription: the callback plus the address of whoever registered
        // it (its `this`). The owner is how a subscriber later unsubscribes -- a
        // single pointer per listener, no extra allocation.
        struct Listener {
            void* owner = nullptr;
            std::function<void(EventType&)> fn;
            std::shared_ptr<std::atomic<bool>> active = std::make_shared<std::atomic<bool>>(true);
            bool main = false;
        };

        struct ListenerGroup {
            ListenerKey key{};
            std::vector<Listener> listeners;
        };

        mutable std::recursive_mutex mutex;
        std::vector<ListenerGroup> listenerGroups;
        Dispatcher() = default;
        Dispatcher(const Dispatcher& other) {
            std::lock_guard lock(other.mutex);
            listenerGroups = other.listenerGroups;
            pendingUnsubscribe = other.pendingUnsubscribe;
            for (auto& group : listenerGroups) for (auto& listener : group.listeners)
                listener.active = std::make_shared<std::atomic<bool>>(listener.active->load());
        }
        Dispatcher(Dispatcher&& other) {
            std::lock_guard lock(other.mutex);
            listenerGroups = std::move(other.listenerGroups);
            pendingUnsubscribe = std::move(other.pendingUnsubscribe);
            other.listenerGroups.clear();
        }
        Dispatcher& operator=(const Dispatcher& other) {
            if (this == &other) return *this;
            Dispatcher copy(other);
            return *this = std::move(copy);
        }
        Dispatcher& operator=(Dispatcher&& other) {
            if (this == &other) return *this;
            std::scoped_lock lock(mutex, other.mutex);
            for (auto& group : listenerGroups) for (auto& listener : group.listeners) *listener.active = false;
            listenerGroups = std::move(other.listenerGroups);
            pendingUnsubscribe = std::move(other.pendingUnsubscribe);
            other.listenerGroups.clear();
            return *this;
        }
        ~Dispatcher() { for (auto& group : listenerGroups) for (auto& listener : group.listeners) *listener.active = false; }

        // Deferred unsubscription. tell() iterates a group's listener vector by reference,
        // so a listener that unsubscribes mid-dispatch (a common, legitimate thing — e.g. a
        // one-shot subscriber that fires then detaches) would invalidate that iteration. So
        // unsubscribe() never removes inline — it only queues here, and tell() drains the
        // queue before it walks. The vector is therefore never mutated under a dispatch.
        std::vector<void*> pendingUnsubscribe;

        template<typename Owner>
        static ListenerKey listenerKey(void (Owner::*func)(EventType&)) {
            static_assert(sizeof(func) <= ListenerKeySize, "Member function pointer is larger than Dispatcher::ListenerKey");

            ListenerKey key{};
            std::memcpy(key.data(), &func, sizeof(func));
            return key;
        }

        // Listen
        //--------------------------------------------------

        void listen(
            ListenerKey listenerKey,
            void* owner,
            const std::function<void(EventType&)>& listener,
            bool main = false
        ) {
            std::lock_guard lock(mutex);
            auto it = std::find_if(
                listenerGroups.begin(),
                listenerGroups.end(),
                [listenerKey](const ListenerGroup& group) {
                    return listenerKey == group.key;
                }
            );

            if (it != listenerGroups.end()) {
                it->listeners.push_back({ owner, listener, std::make_shared<std::atomic<bool>>(true), main });
                return;
            }

            ListenerGroup group;
            group.key = listenerKey;
            group.listeners.push_back({ owner, listener, std::make_shared<std::atomic<bool>>(true), main });
            listenerGroups.push_back(group);
        }

        // Owner-less convenience (a subscription that is never individually removed).
        void listen(
            ListenerKey listenerKey,
            const std::function<void(EventType&)>& listener
        ) {
            listen(listenerKey, nullptr, listener);
        }

        template<typename Owner>
        void listen(
            void (Owner::*func)(EventType&),
            void* owner,
            const std::function<void(EventType&)>& listener
        ) {
            listen(listenerKey(func), owner, listener);
        }

        template<typename Owner>
        void listen(
            void (Owner::*func)(EventType&),
            const std::function<void(EventType&)>& listener
        ) {
            listen(listenerKey(func), nullptr, listener);
        }

        // GUI subscribers receive a copied event in the main-thread mailbox.
        template<typename Owner>
        void listenMain(void (Owner::*func)(EventType&), void* owner,
                        const std::function<void(EventType&)>& listener) {
            static_assert(std::is_copy_constructible_v<EventType>);
            listen(listenerKey(func), owner, listener, true);
        }

        // Unsubscribe
        //--------------------------------------------------

        // Queue removal of every listener registered by `owner`. A subscriber calls this
        // with its own `this` as it is destroyed; a listener may even call it from inside
        // its own callback. Removal is always deferred (drained at the next tell(), before
        // it walks), so it never disturbs a dispatch in flight or holds a dangling callback.
        void unsubscribe(void* owner) {
            if (!owner) { return; }
            std::lock_guard lock(mutex);
            for (auto& group : listenerGroups) for (auto& listener : group.listeners)
                if (listener.owner == owner) *listener.active = false;
            pendingUnsubscribe.push_back(owner);
        }

        void removeOwner(void* owner) {
            for (auto& group : listenerGroups) {
                auto& listeners = group.listeners;
                listeners.erase(
                    std::remove_if(
                        listeners.begin(),
                        listeners.end(),
                        [owner](const Listener& l) { return l.owner == owner; }
                    ),
                    listeners.end()
                );
            }
        }

        // Tell
        //--------------------------------------------------

        void tell(ListenerKey tellingKey, EventType& event) {
            std::vector<Listener> listeners;
            {
                std::lock_guard lock(mutex);
                for (void* owner : pendingUnsubscribe) removeOwner(owner);
                pendingUnsubscribe.clear();
                auto it = std::find_if(listenerGroups.begin(), listenerGroups.end(),
                    [&](const ListenerGroup& group) { return group.key == tellingKey; });
                if (it == listenerGroups.end()) return;
                listeners = it->listeners;
            }
            for (const auto& listener : listeners) {
                if (!*listener.active) continue;
                if (listener.main) {
                    if constexpr (std::is_copy_constructible_v<EventType>) {
                        MainDispatch::instance().post([listener, event]() mutable {
                            if (*listener.active) listener.fn(event);
                        });
                    }
                } else {
                    listener.fn(event);
                }
            }
        }

        template<typename Owner>
        void tell(void (Owner::*func)(EventType&), EventType& event) {
            tell(listenerKey(func), event);
        }
    };
}
