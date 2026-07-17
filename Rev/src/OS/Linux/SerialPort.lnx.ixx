module;

#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <system_error>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/serial.h>

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

        // /dev/ttyS0..31 are the legacy platform 8250 UARTs -- the kernel
        // always registers these (with a "device" symlink to the serial8250
        // platform device) whether or not real hardware is wired up behind
        // them, so the plain existence check above doesn't filter them out.
        // Same probe pyserial's comports() uses: TIOCGSERIAL reports
        // PORT_UNKNOWN for a ttySN with no real UART behind it.
        static bool isRealPort(const std::string& name, const std::string& devicePath) {
            if (name.rfind("ttyS", 0) != 0) return true;   // USB/ACM ports: trust sysfs

            int fd = ::open(devicePath.c_str(), O_RDONLY | O_NONBLOCK | O_NOCTTY);
            if (fd < 0) return false;

            struct serial_struct serinfo{};
            bool real = (::ioctl(fd, TIOCGSERIAL, &serinfo) == 0) && (serinfo.type != PORT_UNKNOWN);
            ::close(fd);
            return real;
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
                if (!isRealPort(name, devicePath)) continue;   // skip phantom ttyS0..31

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
