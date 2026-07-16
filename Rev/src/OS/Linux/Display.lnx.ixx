module;

#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>

export module Rev.OS.Display;

import Rev.NativeWindow;

export namespace Rev::OS {

    struct DisplayInfo {
        std::string id;      // XRandR output name, e.g. "HDMI-1" -- pass back into other calls
        std::string label;   // e.g. "HDMI-1  1920x1080  (primary)"
        int x = 0, y = 0, w = 0, h = 0;
        bool primary = false;
    };

    struct Display {

        static std::vector<DisplayInfo> List() {

            std::vector<DisplayInfo> found;

            for (const auto& d : Rev::NativeWindow::getDisplays()) {

                std::string label = d.friendlyName + "  " +
                    std::to_string(d.w) + "x" + std::to_string(d.h) +
                    (d.primary ? "  (primary)" : "");

                found.push_back({ d.friendlyName, label, d.x, d.y, d.w, d.h, d.primary });
            }

            return found;
        }

        // Best-effort: switches to a mode if it already exists on the output
        // (e.g. the projector's EDID already advertises 640x360). Does NOT
        // create a new custom mode -- that needs `cvt`/`xrandr --newmode`/
        // `--addmode`, which is driver- and distro-dependent enough that it's
        // out of scope here. Returns false (and leaves the mode unchanged) if
        // the target mode isn't already available.
        static bool SetMode(const std::string& id, int width, int height) {

            std::string command = "command -v xrandr >/dev/null 2>&1 && xrandr --output '" + id +
                "' --mode " + std::to_string(width) + "x" + std::to_string(height) +
                " >/dev/null 2>&1";
            return std::system(command.c_str()) == 0;
        }
    };
}
