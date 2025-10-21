
module;

#include <ctime>
#include <chrono>

export module Rev.Element.Event;

import Rev.Graphics.Canvas;
import Rev.GlobalTime;
import Rev.Core.Pos;
import Rev.Element.Style;

export namespace Rev::Element {

    using namespace Rev::Core;

    struct Event {

        struct Button {

            int id = 0;
            std::chrono::steady_clock::time_point lastPressTime;
            int pressTimeDiff = 0;

            // The button has a pos so we know when we last clicked it
            Pos lastPressPos;
            Pos pressPosDiff;

            void set(bool down, Pos pos = Pos(0, 0)) {

                if (down) { this->press(pos); }
                else { this->release(); }
            }

            void press(Pos& pos) {

                // Change id to positive
                id = abs(id);
                id += 1;

                // Set last press time
                auto now = std::chrono::steady_clock::now();
                pressTimeDiff = std::chrono::duration<double, std::milli>(now - lastPressTime).count();
                lastPressTime = now;

                // Set last press pos
                pressPosDiff = (pos - lastPressPos);
                lastPressPos = pos;
            }

            void release() {

                // Change to negative
                id = -1 * abs(id);
            }

            // Return whether this is a double click (must be within length / time window)
            bool isDoubleClick(float timeThresh = 200, float lenThresh = 10) {
                return (
                    pressTimeDiff < timeThresh &&
                    pressPosDiff.pythag() < lenThresh
                );
            }

            // Return true if pressed
            operator bool() {
                return (this->id > 0);
            }
        };

        struct Mouse {

            Cursor cursor = Cursor::Unset;

            Pos pos, down, up;
            Pos drag, diff;
            Pos dragStart, dragEnd;
            Pos wheel;

            Button lb, mb, rb;

            Mouse() {}
        };

        struct Keyboard {

            Button ctrl, alt, shift;
        };

        Mouse mouse;
        Keyboard keyboard;

        bool propagate = true;
        bool causedRefresh = false;
        uint64_t firstTime = 0; uint64_t time = 0;
        int id = 0;

        Graphics::Canvas* canvas;

        // Set time
        void resetBeforeDispatch() {

            GlobalTime::Update();
            
            time = GlobalTime::CurrentMs();
            propagate = true;
            causedRefresh = false;
        }
    };
};