module;

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <unistd.h>
#include <vector>
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

        // Abort any in-flight blocking read so a thread stuck in readLine()/readBytes()
        // returns immediately. Safe to call from another thread (e.g. an E-STOP handler
        // while a job thread is blocked waiting for a gantry response). Only the receive
        // side is aborted, so a concurrent emergency write is left intact.
        void cancel() {

            if (handle < 0) return;

            // TCFLSH/TCIFLUSH come from the same asm/termbits.h family used
            // for TCGETS2/TCSETS2 above -- avoid <termios.h> here, it
            // redeclares struct termios and conflicts with asm/termbits.h.
            ioctl(handle, TCFLSH, TCIFLUSH);
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

        // Read bytes until '\n' (strips '\r'), returns empty string on timeout
        std::string readLine(int timeoutMs = 5000) {

            if (!connected()) return "";

            std::string result;
            pollfd pfd{ handle, POLLIN, 0 };

            for (;;) {

                int remaining = timeoutMs;
                int rc = poll(&pfd, 1, remaining);

                if (rc <= 0) break; // timeout or error

                char ch;
                ssize_t n = read(handle, &ch, 1);

                if (n <= 0) {
                    if (n < 0 && errno == EINTR) continue;
                    break;
                }

                if (ch == '\n') break;
                if (ch != '\r') result += ch;
            }

            dbg("[Serial] readLine -> '%s'", result.c_str());
            return result;
        }

        // Read exactly n bytes, returns fewer on timeout
        std::vector<uint8_t> readBytes(size_t n, int timeoutMs = 5000) {

            if (!connected() || n == 0) return {};

            std::vector<uint8_t> result(n);
            size_t total = 0;
            pollfd pfd{ handle, POLLIN, 0 };

            while (total < n) {

                int rc = poll(&pfd, 1, timeoutMs);
                if (rc <= 0) break;

                ssize_t rd = read(handle, result.data() + total, n - total);
                if (rd <= 0) {
                    if (rd < 0 && errno == EINTR) continue;
                    break;
                }

                total += static_cast<size_t>(rd);
            }

            result.resize(total);

            dbg("[Serial] readBytes(%zu) -> got %zu", n, total);
            return result;
        }
    };
};
