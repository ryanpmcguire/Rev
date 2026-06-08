module;

#include <string>

#include <windows.h>
#include <vector>

#include <dbg.hpp>

export module Rev.Serial;

export namespace Rev {

    struct Serial {

        HANDLE handle = INVALID_HANDLE_VALUE;

        std::string port;
        int baud = 0;

        Serial(const std::string& port, int baud)
            : port(port), baud(baud) {

            dbg("[Serial] Opening %s @ %d", port.c_str(), baud);

            std::string path = "\\\\.\\" + port;

            handle = CreateFileA(
                path.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr
            );

            if (handle == INVALID_HANDLE_VALUE) {

                dbg("[Serial] Failed to open port");
                return;
            }

            DCB dcb{};
            dcb.DCBlength = sizeof(DCB);

            if (!GetCommState(handle, &dcb)) {

                dbg("[Serial] GetCommState failed");

                CloseHandle(handle);
                handle = INVALID_HANDLE_VALUE;

                return;
            }

            dcb.BaudRate = baud;
            dcb.ByteSize = 8;
            dcb.StopBits = ONESTOPBIT;
            dcb.Parity = NOPARITY;

            if (!SetCommState(handle, &dcb)) {

                dbg("[Serial] SetCommState failed");

                CloseHandle(handle);
                handle = INVALID_HANDLE_VALUE;

                return;
            }

            COMMTIMEOUTS timeouts{};
            timeouts.ReadIntervalTimeout = 50;
            timeouts.ReadTotalTimeoutConstant = 50;
            timeouts.ReadTotalTimeoutMultiplier = 10;
            timeouts.WriteTotalTimeoutConstant = 50;
            timeouts.WriteTotalTimeoutMultiplier = 10;

            SetCommTimeouts(handle, &timeouts);

            dbg("[Serial] Connected");
        }

        ~Serial() {

            dbg("[Serial] Closing");

            if (handle != INVALID_HANDLE_VALUE) {

                CloseHandle(handle);
            }
        }

        bool connected() const {

            return handle != INVALID_HANDLE_VALUE;
        }

        void sendBytes(const void* data, size_t size) {

            if (!connected()) {

                dbg("[Serial] Not connected");
                return;
            }

            DWORD written = 0;

            BOOL ok = WriteFile(
                handle,
                data,
                (DWORD)size,
                &written,
                nullptr
            );

            if (!ok) {

                dbg("[Serial] Write failed");
            }

            else {

                dbg("[Serial] Sent %d bytes", (int)written);
            }
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

            COMMTIMEOUTS to{};
            to.ReadIntervalTimeout         = 0;
            to.ReadTotalTimeoutConstant    = (DWORD)timeoutMs;
            to.ReadTotalTimeoutMultiplier  = 0;
            to.WriteTotalTimeoutConstant   = 50;
            to.WriteTotalTimeoutMultiplier = 10;
            SetCommTimeouts(handle, &to);

            std::string result;
            char ch;
            DWORD rd;

            while (ReadFile(handle, &ch, 1, &rd, nullptr) && rd == 1) {
                if (ch == '\n') break;
                if (ch != '\r') result += ch;
            }

            // Restore default timeouts
            COMMTIMEOUTS restore{};
            restore.ReadIntervalTimeout         = 50;
            restore.ReadTotalTimeoutConstant    = 50;
            restore.ReadTotalTimeoutMultiplier  = 10;
            restore.WriteTotalTimeoutConstant   = 50;
            restore.WriteTotalTimeoutMultiplier = 10;
            SetCommTimeouts(handle, &restore);

            dbg("[Serial] readLine -> '%s'", result.c_str());
            return result;
        }

        // Read exactly n bytes, returns fewer on timeout
        std::vector<uint8_t> readBytes(size_t n, int timeoutMs = 5000) {

            if (!connected() || n == 0) return {};

            COMMTIMEOUTS to{};
            to.ReadIntervalTimeout         = 0;
            to.ReadTotalTimeoutConstant    = (DWORD)timeoutMs;
            to.ReadTotalTimeoutMultiplier  = 0;
            to.WriteTotalTimeoutConstant   = 50;
            to.WriteTotalTimeoutMultiplier = 10;
            SetCommTimeouts(handle, &to);

            std::vector<uint8_t> result(n);
            DWORD total = 0;

            while (total < (DWORD)n) {
                DWORD rd = 0;
                if (!ReadFile(handle, result.data() + total, (DWORD)n - total, &rd, nullptr) || rd == 0) break;
                total += rd;
            }

            result.resize(total);

            COMMTIMEOUTS restore{};
            restore.ReadIntervalTimeout         = 50;
            restore.ReadTotalTimeoutConstant    = 50;
            restore.ReadTotalTimeoutMultiplier  = 10;
            restore.WriteTotalTimeoutConstant   = 50;
            restore.WriteTotalTimeoutMultiplier = 10;
            SetCommTimeouts(handle, &restore);

            dbg("[Serial] readBytes(%zu) -> got %lu", n, total);
            return result;
        }
    };
};