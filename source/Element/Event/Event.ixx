
module;

#include <string>
#include <string_view>

#include <ctime>
#include <chrono>

export module Rev.Element.Event;

import Rev.Graphics.Canvas;
import Rev.GlobalTime;
import Rev.Core.Pos;
import Rev.Appearance;

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

            Appearance::Cursor cursor = Appearance::Cursor::Unset;

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
            Button num0, num1, num2, num3, num4;
            Button num5, num6, num7, num8, num9;
            Button a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z;

            Arrows arrows;

            std::string key;
            std::string input;

            // A canonical "ctrl+shift+s"-style string for the current chord, built
            // once by the window (buildCombo) when it sets the keyboard state. Just
            // compare it directly:  if (e.keyboard.combo == "ctrl+s") { ... }
            // No per-call parsing, and no key-name casing to worry about.
            std::string combo;

            // Modifiers in a fixed order (ctrl, shift, alt), then the key, all
            // lower-case and joined with '+'. The key is omitted when it *is* a
            // modifier (already represented). e.g. Ctrl+Shift+S -> "ctrl+shift+s".
            // The match is exact, so combo == "ctrl+s" is false while Shift is held.
            void buildCombo() {

                combo = "";

                auto add = [this](const std::string& part) {
                    if (!combo.empty()) { combo += "+"; }
                    combo += part;
                };

                if (ctrl)  { add("ctrl"); }
                if (shift) { add("shift"); }
                if (alt)   { add("alt"); }

                auto addCharacter = [&add](Button& button, char character) {
                    if (button) { add(std::string(1, character)); }
                };

                addCharacter(a, 'a'); addCharacter(b, 'b'); addCharacter(c, 'c');
                addCharacter(d, 'd'); addCharacter(e, 'e'); addCharacter(f, 'f');
                addCharacter(g, 'g'); addCharacter(h, 'h'); addCharacter(i, 'i');
                addCharacter(j, 'j'); addCharacter(k, 'k'); addCharacter(l, 'l');
                addCharacter(m, 'm'); addCharacter(n, 'n'); addCharacter(o, 'o');
                addCharacter(p, 'p'); addCharacter(q, 'q'); addCharacter(r, 'r');
                addCharacter(s, 's'); addCharacter(t, 't'); addCharacter(u, 'u');
                addCharacter(v, 'v'); addCharacter(w, 'w'); addCharacter(x, 'x');
                addCharacter(y, 'y'); addCharacter(z, 'z');
                addCharacter(num0, '0'); addCharacter(num1, '1');
                addCharacter(num2, '2'); addCharacter(num3, '3');
                addCharacter(num4, '4'); addCharacter(num5, '5');
                addCharacter(num6, '6'); addCharacter(num7, '7');
                addCharacter(num8, '8'); addCharacter(num9, '9');

                const bool keyIsCharacter =
                    key.size() == 1 && (
                        (key.front() >= 'a' && key.front() <= 'z') ||
                        (key.front() >= '0' && key.front() <= '9')
                    );
                if (
                    !keyIsCharacter && !key.empty() &&
                    key != "ctrl" && key != "shift" && key != "alt"
                ) {
                    add(key);
                }
            }
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

            mouse.cursor = Appearance::Cursor::Unset;
        }
    };
};
