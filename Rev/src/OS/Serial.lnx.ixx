module;

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <termios.h>
#include <unistd.h>

#include <dbg.hpp>

export module Rev.Serial;

export namespace Rev {

    struct Serial {

        int handle = -1;

        std::string port;
        int baud = 0;

        static speed_t baudToSpeed(int baud) {
            #ifdef B250000
            if (baud == 250000) return B250000;
            #endif
            switch (baud) {
                case 9600: return B9600;
                case 19200: return B19200;
                case 38400: return B38400;
                case 57600: return B57600;
                case 115200: return B115200;
#ifdef B230400
                case 230400: return B230400;
#endif
                default: return B115200;
            }
        }

        Serial(const std::string& port, int baud)
            : port(port), baud(baud) {

            dbg("[Serial] Opening %s @ %d", port.c_str(), baud);

            handle = open(port.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
            if (handle < 0) {
                dbg("[Serial] Failed to open port");
                return;
            }

            termios tty{};
            if (tcgetattr(handle, &tty) != 0) {
                dbg("[Serial] tcgetattr failed");
                close(handle);
                handle = -1;
                return;
            }

            cfmakeraw(&tty);
            speed_t speed = baudToSpeed(baud);
            cfsetispeed(&tty, speed);
            cfsetospeed(&tty, speed);

            tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
            tty.c_cflag |= CLOCAL | CREAD;
            tty.c_cflag &= ~(PARENB | PARODD);
            tty.c_cflag &= ~CSTOPB;
            tty.c_cflag &= ~CRTSCTS;
            tty.c_cc[VMIN] = 0;
            tty.c_cc[VTIME] = 5;

            if (tcsetattr(handle, TCSANOW, &tty) != 0) {
                dbg("[Serial] tcsetattr failed: %s", strerror(errno));
                close(handle);
                handle = -1;
                return;
            }

            if (speed == B115200 && baud != 115200) {
                dbg("[Serial] Unsupported baud %d, using 115200", baud);
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

            ssize_t written = write(handle, data, size);
            if (written < 0) dbg("[Serial] Write failed");
            else dbg("[Serial] Sent %d bytes", (int)written);
        }

        void sendText(const std::string& text) {
            sendBytes(text.data(), text.size());
        }

        void sendByte(uint8_t byte) {
            sendBytes(&byte, 1);
        }
    };
};
