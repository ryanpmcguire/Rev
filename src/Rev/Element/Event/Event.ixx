module;

#include <chrono>

export module Rev.Event;

import Rev.GlobalTime;
import Rev.Pos;

export namespace Rev {

    struct Event {

        struct Mouse {
            Pos pos;
        };

        Mouse mouse;

        bool propagate = true;
        bool causedRefresh = false;
        uint64_t firstTime = 0; uint32_t time = 0;

        // Set time
        void resetBeforeDispatch() {

            GlobalTime::Update();
            
            time = GlobalTime::CurrentMs();
            propagate = true;
            causedRefresh = false;
        }
    };
};