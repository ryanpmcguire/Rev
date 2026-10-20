module;

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <asm/termbits.h>
#include <sys/ioctl.h>

#include <dbg.hpp>

export module Rev.Serial;

export namespace Rev {

    struct Serial {

        int handle = -1;

        std::string port;
        int baud = 0;

        static unsigned int baudToConstant(int baud) {
            switch (baud) {
                case 9600: return B9600;
                case 19200: return B19200;
                case 38400: return B38400;
                case 57600: return B57600;
                case 115200: return B115200;
#ifdef B230400
                case 230400: return B230400;
#endif
#ifdef B250000
                case 250000: return B250000;
#endif
                default: return 0;
            }
        }

        bool configurePort(int requestedBaud) {
            termios2 tty{};
            if (ioctl(handle, TCGETS2, &tty) != 0) {
                dbg("[Serial] TCGETS2 failed: %s", strerror(errno));
                return false;
            }

            tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
            tty.c_oflag &= ~OPOST;
            tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
            tty.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS);
            tty.c_cflag |= CS8 | CLOCAL | CREAD;
            tty.c_cc[VMIN] = 0;
            tty.c_cc[VTIME] = 5;

            unsigned int speed = baudToConstant(requestedBaud);
            if (speed != 0) {
                tty.c_cflag &= ~CBAUD;
                tty.c_cflag |= speed;
                tty.c_ispeed = requestedBaud;
                tty.c_ospeed = requestedBaud;
            }
            else {
#ifdef BOTHER
                tty.c_cflag &= ~CBAUD;
                tty.c_cflag |= BOTHER;
                tty.c_ispeed = requestedBaud;
                tty.c_ospeed = requestedBaud;
#else
                dbg("[Serial] Unsupported baud %d and BOTHER is unavailable", requestedBaud);
                return false;
#endif
            }

            if (ioctl(handle, TCSETS2, &tty) != 0) {
                dbg("[Serial] TCSETS2 failed: %s", strerror(errno));
                return false;
            }

            return true;
        }

        Serial(const std::string& port, int baud)
            : port(port), baud(baud) {

            dbg("[Serial] Opening %s @ %d", port.c_str(), baud);

            handle = open(port.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
            if (handle < 0) {
                dbg("[Serial] Failed to open port");
                return;
            }

            if (!configurePort(baud)) {
                close(handle);
                handle = -1;
                return;
            }

            dbg("[Serial] Connected");
        }

        ~Serial() {
            dbg("[Serial] Closing");
            if (handle >= 0) close(handle);
        }

        bool connected() const {
            return handle >= 0;
        }

        void sendBytes(const void* data, size_t size) {
            if (!connected()) {
                dbg("[Serial] Not connected");
                return;
            }

            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            size_t total = 0;
            while (total < size) {
                ssize_t written = write(handle, bytes + total, size - total);
                if (written < 0) {
                    if (errno == EINTR) continue;
                    dbg("[Serial] Write failed: %s", strerror(errno));
                    return;
                }
                if (written == 0) break;
                total += static_cast<size_t>(written);
            }
            dbg("[Serial] Sent %d bytes", (int)total);
        }

        void sendText(const std::string& text) {
            sendBytes(text.data(), text.size());
        }

        void sendByte(uint8_t byte) {
            sendBytes(&byte, 1);
        }
    };
};
