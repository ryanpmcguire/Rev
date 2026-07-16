module;

#include <string>
#include <vector>
#include <cstring>

#include <windows.h>
#include <setupapi.h>
#pragma comment(lib, "setupapi.lib")

export module Rev.OS.SerialPort;

export namespace Rev::OS {

    struct SerialPortInfo {
        std::string device;    // e.g. "COM3"
        std::string label;     // e.g. "COM3 -- USB Serial Port"
    };

    struct SerialPort {

        static std::vector<SerialPortInfo> List() {

            static const GUID PORTS_GUID = {
                0x4d36e978, 0xe325, 0x11ce,
                { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 }
            };

            std::vector<SerialPortInfo> found;

            HDEVINFO devInfo = SetupDiGetClassDevsA(&PORTS_GUID, nullptr, nullptr, DIGCF_PRESENT);
            if (devInfo == INVALID_HANDLE_VALUE) return found;

            SP_DEVINFO_DATA devData{};
            devData.cbSize = sizeof(devData);

            for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devData); ++i) {

                char friendlyName[256] = {};
                SetupDiGetDeviceRegistryPropertyA(devInfo, &devData, SPDRP_FRIENDLYNAME,
                    nullptr, (PBYTE)friendlyName, sizeof(friendlyName), nullptr);

                char portName[32] = {};
                HKEY hKey = SetupDiOpenDevRegKey(devInfo, &devData,
                    DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
                if (hKey != INVALID_HANDLE_VALUE) {
                    DWORD portLen = sizeof(portName);
                    RegQueryValueExA(hKey, "PortName", nullptr, nullptr,
                        (LPBYTE)portName, &portLen);
                    RegCloseKey(hKey);
                }

                if (portName[0] == '\0' || strncmp(portName, "COM", 3) != 0) continue;

                std::string friendly(friendlyName);
                // Strip the redundant " (COMx)" suffix that Windows appends
                std::string suffix = " (" + std::string(portName) + ")";
                auto spos = friendly.rfind(suffix);
                if (spos != std::string::npos) friendly.erase(spos);

                std::string label;
                if (!friendly.empty())
                    label = std::string(portName) + " -- " + friendly;
                else
                    label = std::string(portName);

                found.push_back({ std::string(portName), label });
            }

            SetupDiDestroyDeviceInfoList(devInfo);
            return found;
        }
    };
}
