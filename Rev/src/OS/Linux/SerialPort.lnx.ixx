module;

#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <system_error>

export module Rev.OS.SerialPort;

export namespace Rev::OS {

    struct SerialPortInfo {
        std::string device;    // e.g. "/dev/ttyUSB0"
        std::string label;     // e.g. "/dev/ttyUSB0 -- FTDI USB Serial"
    };

    struct SerialPort {

        static std::string readFirstLine(const std::filesystem::path& p) {
            std::ifstream f(p);
            std::string line;
            if (f) std::getline(f, line);
            return line;
        }

        // Walk up from the tty's resolved sysfs device directory looking for
        // USB "manufacturer"/"product" attribute files, same idea as reading
        // the friendly name out of the Windows registry.
        static std::string friendlyName(const std::filesystem::path& deviceDir) {
            std::error_code ec;
            std::filesystem::path dir = std::filesystem::canonical(deviceDir, ec);
            if (ec) return "";

            for (int depth = 0; depth < 6 && !dir.empty(); depth++) {

                std::string manufacturer = readFirstLine(dir / "manufacturer");
                std::string product      = readFirstLine(dir / "product");

                if (!manufacturer.empty() || !product.empty()) {
                    if (!manufacturer.empty() && !product.empty()) return manufacturer + " " + product;
                    return manufacturer.empty() ? product : manufacturer;
                }

                std::filesystem::path parent = dir.parent_path();
                if (parent == dir) break;
                dir = parent;
            }

            return "";
        }

        static std::vector<SerialPortInfo> List() {

            std::vector<SerialPortInfo> found;
            std::error_code ec;

            const std::filesystem::path ttyClassDir = "/sys/class/tty";
            if (!std::filesystem::exists(ttyClassDir, ec)) return found;

            for (const auto& entry : std::filesystem::directory_iterator(ttyClassDir, ec)) {

                std::filesystem::path devLink = entry.path() / "device";
                if (!std::filesystem::exists(devLink, ec)) continue;   // skip virtual ttys (tty0..63, ptmx, ...)

                std::string name = entry.path().filename().string();
                std::string devicePath = "/dev/" + name;
                if (!std::filesystem::exists(devicePath, ec)) continue;

                std::string friendly = friendlyName(devLink);

                std::string label = friendly.empty()
                    ? devicePath
                    : devicePath + " -- " + friendly;

                found.push_back({ devicePath, label });
            }

            return found;
        }
    };
}
