module;

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

export module Rev.Core.Dispatcher;

export namespace Rev::Core {

    template<typename EventType>
    struct Dispatcher {

        using ListenerKey = std::uintptr_t;

        struct ListenerGroup {
            ListenerKey key = 0;
            std::vector<std::function<void(EventType&)>> listeners;
        };

        std::vector<ListenerGroup> listenerGroups;

        template<typename Owner>
        static ListenerKey listenerKey(void (Owner::*func)(EventType&)) {

            ListenerKey key = 0;
            std::memcpy(&key, &func, sizeof(func));
            return key;
        }

        void listen(
            ListenerKey listenerKey,
            const std::function<void(EventType&)>& listener
        ) {

            auto it = std::find_if(
                listenerGroups.begin(),
                listenerGroups.end(),
                [listenerKey](const ListenerGroup& group) {
                    return listenerKey == group.key;
                }
            );

            if (it != listenerGroups.end()) {
                it->listeners.push_back(listener);
                return;
            }

            ListenerGroup group;
            group.key = listenerKey;
            group.listeners.push_back(listener);
            listenerGroups.push_back(group);
        }

        void tell(ListenerKey tellingKey, EventType& event) {

            auto it = std::find_if(
                listenerGroups.begin(),
                listenerGroups.end(),
                [tellingKey](const ListenerGroup& group) {
                    return tellingKey == group.key;
                }
            );

            if (it == listenerGroups.end()) {
                return;
            }

            for (auto& listener : it->listeners) {
                listener(event);
            }
        }

        template<typename Owner>
        void listen(
            void (Owner::*func)(EventType&),
            const std::function<void(EventType&)>& listener
        ) {
            listen(listenerKey(func), listener);
        }

        template<typename Owner>
        void tell(void (Owner::*func)(EventType&), EventType& event) {
            tell(listenerKey(func), event);
        }
    };
}
