
module;

#include <string>
#include <string_view>

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

            // Rev's cross-platform double-click (time + distance since last press).
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

            // Absolute screen position (physical px) of the cursor, supplied by
            // the native layer. Independent of this window's origin, so drag /
            // resize math built on it stays stable while the window itself moves.
            Pos screenPos;

            Pos drag, diff;
            Pos dragStart, dragEnd;
            Pos wheel;

            Button lb, mb, rb;

            Mouse() {}
        };

        struct Keyboard {

            struct Arrows {
                Button left, right, up, down;
            };

            Button ctrl, alt, shift;
            Button escape, tab, del, backspace;

            Button enter;
            Button space;

            Arrows arrows;

            std::string key;
            std::string input;
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

            mouse.cursor = Cursor::Unset;
        }
    };
};