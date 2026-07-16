module;

#include <string>
#include <vector>

#include <windows.h>

export module Rev.OS.Display;

export namespace Rev::OS {

    struct DisplayInfo {
        std::string id;      // e.g. "\\.\DISPLAY1" -- pass back into other calls
        std::string label;   // e.g. "\\.\DISPLAY1  1920x1080  (primary)"
        int x = 0, y = 0, w = 0, h = 0;
        bool primary = false;
    };

    struct Display {

        static std::vector<DisplayInfo> List() {

            std::vector<DisplayInfo> found;

            DISPLAY_DEVICEA dd{};
            dd.cb = sizeof(dd);

            for (DWORD i = 0; EnumDisplayDevicesA(nullptr, i, &dd, 0); ++i) {

                if (!(dd.StateFlags & DISPLAY_DEVICE_ACTIVE)) continue;

                DEVMODEA dm{};
                dm.dmSize = sizeof(dm);
                EnumDisplaySettingsA(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm);

                bool primary = !!(dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE);
                std::string label = std::string(dd.DeviceName) + "  " +
                    std::to_string(dm.dmPelsWidth) + "x" + std::to_string(dm.dmPelsHeight) +
                    (primary ? "  (primary)" : "");

                found.push_back({
                    std::string(dd.DeviceName),
                    label,
                    (int)dm.dmPosition.x,
                    (int)dm.dmPosition.y,
                    (int)dm.dmPelsWidth,
                    (int)dm.dmPelsHeight,
                    primary
                });
            }

            return found;
        }

        // Force a display to a specific mode (used to set the projector's
        // native 640x360 resolution). Windows-only -- see comment at the
        // Linux SetMode() stub.
        static bool SetMode(const std::string& id, int width, int height) {

            DEVMODEA dm{};
            dm.dmSize       = sizeof(dm);
            dm.dmPelsWidth  = width;
            dm.dmPelsHeight = height;
            dm.dmBitsPerPel = 32;
            dm.dmFields     = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;

            LONG r = ChangeDisplaySettingsExA(id.c_str(), &dm, nullptr, CDS_UPDATEREGISTRY, nullptr);
            return r == DISP_CHANGE_SUCCESSFUL;
        }
    };
}
