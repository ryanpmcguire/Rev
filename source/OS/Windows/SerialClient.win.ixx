module;

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <cstdint>

#include <windows.h>

#include <dbg.hpp>

export module Rev.SerialClient;

import Rev.Transport;
import Rev.Core.Process;

export namespace Rev {

    // ------------------------------------------------------------------
    // SerialClient  — the Win32 serial (USB-CDC virtual COM) implementation of
    // the Rev::Transport byte-pipe. It mirrors Rev::Client's threading contract
    // EXACTLY: a worker thread owns the port, reads bytes and enqueues them via
    // the inherited fire* helpers, and the main-thread pump() (from Transport)
    // drains the queue so every listener runs single-threaded. Only the winsock
    // calls are swapped for CreateFile / ReadFile / WriteFile.
    //
    //   Rev::SerialClient* c = new Rev::SerialClient();
    //   c->onData([](Rev::Transport::DataEvent& e) { ... });
    //   c->connect("COM3", 115200);
    //   c->send("?\n");
    //   delete c;  // disconnects and joins the worker thread
    //
    // Reads are BYTE-oriented and fragmented — a read may return a single byte
    // or a partial line. The adapter's decoder reassembles lines; this class
    // never assumes a whole line/packet per read.
    // ------------------------------------------------------------------

    struct SerialClient : public Transport {

        // -- Construction / destruction ----------------------------------

        SerialClient() = default;

        ~SerialClient() override {
            dbg("[SerialClient] Shutting down");

            stopPump();

            running     = false;
            isConnected = false;

            // Atomic exchange: exactly one thread ever closes the handle.
            HANDLE h = handle_.exchange(INVALID_HANDLE_VALUE);
            if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); }

            if (worker_.joinable()) worker_.join();
        }

        // -- Connection --------------------------------------------------

        // endpoint = COM port (e.g. "COM3"); param = baud (default 115200).
        void connect(std::string port, int baud) override {

            if (running) {
                dbg("[SerialClient] connect() called while already running");
                return;
            }

            // Never assign over a joinable std::thread (std::terminate). A prior
            // worker may have exited on its own (open failure) leaving the thread
            // joinable — join it first. Same invariant Rev::Client rests on.
            if (worker_.joinable()) {
                worker_.join();
            }

            port_ = std::move(port);
            baud_ = baud;

            running     = true;
            isConnected = false;

            worker_ = std::thread([this]() { workerMain(); });

            startPump();
        }

        // -- API ---------------------------------------------------------

        void send(const std::string& msg) override {
            HANDLE h = handle_.load();
            if (h == INVALID_HANDLE_VALUE) {
                dbg("[SerialClient] send() on closed port");
                return;
            }
            // One owner thread owns the port, but posted commands arrive on the
            // main thread; serialize the raw WriteFile so two writes can't
            // interleave on the wire.
            std::lock_guard<std::mutex> lock(sendMutex_);
            std::size_t sent = 0;
            while (sent < msg.size()) {
                DWORD written = 0;
                const DWORD remaining = static_cast<DWORD>(msg.size() - sent);
                const BOOL ok = WriteFile(h, msg.data() + sent, remaining, &written, nullptr);
                if (!ok) {
                    dbg("[SerialClient] Write failed after %zu/%zu bytes (%lu)", sent, msg.size(), GetLastError());
                    return;
                }
                if (written == 0) {
                    dbg("[SerialClient] Write made no progress after %zu/%zu bytes", sent, msg.size());
                    return;
                }
                sent += static_cast<std::size_t>(written);
            }
        }

        void disconnect() override {

            if (!running && !worker_.joinable()) { return; }

            const bool wasActive = running;

            running     = false;
            isConnected = false;

            // Reads are bounded to ~50ms (see COMMTIMEOUTS in workerMain), so the
            // worker observes running == false within one timeout; closing the
            // handle here just releases the port. Atomic exchange guarantees a
            // single CloseHandle even if the worker's cleanup races us.
            HANDLE h = handle_.exchange(INVALID_HANDLE_VALUE);
            if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); }

            if (worker_.joinable() && std::this_thread::get_id() != worker_.get_id()) {
                worker_.join();
            }

            // The read loop exits silently on our own close; announce a
            // user-initiated disconnect of a live link here (only if it was live).
            if (wasActive) {
                fireDisconnect();
            }

            pump();
            stopPump();
        }

        // -- Enumeration -------------------------------------------------

        // Every serial port the OS currently exposes, by name (e.g. "COM3").
        // No VID/PID filter — the stock Carvera controller lists all ports and
        // lets the user pick. Reads HKLM\HARDWARE\DEVICEMAP\SERIALCOMM.
        static std::vector<std::string> enumeratePorts() {

            std::vector<std::string> ports;

            HKEY key;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                              "HARDWARE\\DEVICEMAP\\SERIALCOMM",
                              0, KEY_READ, &key) != ERROR_SUCCESS) {
                return ports;   // no serial ports present
            }

            char  name[256];
            char  data[256];
            DWORD index = 0;

            while (true) {
                DWORD nameLen = sizeof(name);
                DWORD dataLen = sizeof(data);
                DWORD type    = 0;

                LONG r = RegEnumValueA(key, index++, name, &nameLen, nullptr,
                                       &type, reinterpret_cast<BYTE*>(data), &dataLen);
                if (r == ERROR_NO_MORE_ITEMS) { break; }
                if (r == ERROR_SUCCESS && type == REG_SZ) {
                    ports.emplace_back(data);   // value data is the "COMx" name
                }
            }

            RegCloseKey(key);
            return ports;
        }

    private:

        // -- Worker state ------------------------------------------------

        std::string          port_;
        int                  baud_       = 115200;
        std::atomic<HANDLE>  handle_     { INVALID_HANDLE_VALUE };
        std::thread          worker_;
        std::mutex           sendMutex_;   // serializes the raw WriteFile across threads

        // -- Worker ------------------------------------------------------

        void workerMain() {

            std::string address = port_ + " @ " + std::to_string(baud_);
            fireConnecting(address);

            // Open \\.\COMx (the \\.\ prefix is required for COM10 and above).
            std::string path = "\\\\.\\" + port_;
            HANDLE h = CreateFileA(
                path.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,               // no sharing
                nullptr,
                OPEN_EXISTING,
                0,               // synchronous I/O
                nullptr
            );

            if (h == INVALID_HANDLE_VALUE) {
                dbg("[SerialClient] Failed to open %s (%lu)", port_.c_str(), GetLastError());
                fireError("Failed to open " + port_);
                running = false;
                return;
            }

            // DCB: 115200 8-N-1, NO flow control (matches the stock controller).
            DCB dcb{};
            dcb.DCBlength = sizeof(DCB);
            if (!GetCommState(h, &dcb)) {
                dbg("[SerialClient] GetCommState failed");
                fireError("GetCommState failed");
                CloseHandle(h);
                running = false;
                return;
            }

            dcb.BaudRate        = (DWORD)baud_;
            dcb.ByteSize        = 8;
            dcb.Parity          = NOPARITY;
            dcb.StopBits        = ONESTOPBIT;
            dcb.fBinary         = TRUE;
            dcb.fOutxCtsFlow    = FALSE;
            dcb.fOutxDsrFlow    = FALSE;
            dcb.fDtrControl     = DTR_CONTROL_DISABLE;
            dcb.fRtsControl     = RTS_CONTROL_DISABLE;
            dcb.fDsrSensitivity = FALSE;
            dcb.fOutX           = FALSE;
            dcb.fInX            = FALSE;

            if (!SetCommState(h, &dcb)) {
                dbg("[SerialClient] SetCommState failed");
                fireError("SetCommState failed");
                CloseHandle(h);
                running = false;
                return;
            }

            // Byte-oriented bounded-poll reads (the documented COMMTIMEOUTS
            // idiom): interval = MAXDWORD with multiplier = MAXDWORD and a small
            // constant means ReadFile returns IMMEDIATELY with whatever bytes are
            // already buffered, or — if the buffer is empty — waits until the
            // first byte arrives or 50ms elapses, whichever is first. The total
            // timeout never scales with the bytes requested, so a SILENT port
            // (e.g. right after the DTR reboot pulse) returns within ~50ms and
            // the loop re-checks running each iteration; disconnect is prompt
            // and never depends on CloseHandle interrupting a long read.
            COMMTIMEOUTS timeouts{};
            timeouts.ReadIntervalTimeout         = MAXDWORD;
            timeouts.ReadTotalTimeoutConstant    = 50;
            timeouts.ReadTotalTimeoutMultiplier  = MAXDWORD;
            timeouts.WriteTotalTimeoutConstant   = 50;
            timeouts.WriteTotalTimeoutMultiplier = 10;
            SetCommTimeouts(h, &timeouts);

            handle_.store(h);

            // DTR reset pulse: reboots the Smoothie/MCU board (intended). Drop
            // DTR, settle 500ms, flush RX/TX, raise DTR, settle 500ms — then the
            // board is freshly booted and ready to stream.
            EscapeCommFunction(h, CLRDTR);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
            EscapeCommFunction(h, SETDTR);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            if (!running) {   // disconnected during the ~1s settle
                isConnected = false;
                HANDLE cur = handle_.exchange(INVALID_HANDLE_VALUE);
                if (cur != INVALID_HANDLE_VALUE) { CloseHandle(cur); }
                return;
            }

            dbg("[SerialClient] Connected to %s", address.c_str());
            isConnected = true;
            fireConnect(address);

            readLoop(h);

            // Cleanup — atomic exchange: only one of worker/disconnect closes.
            isConnected = false;
            HANDLE cur = handle_.exchange(INVALID_HANDLE_VALUE);
            if (cur != INVALID_HANDLE_VALUE) { CloseHandle(cur); }
            running = false;
        }

        void readLoop(HANDLE h) {

            char buffer[4096];
            auto lastBeat = std::chrono::steady_clock::now();

            while (running) {

                // Heartbeat: pulse the status-poll byte at the configured cadence.
                // The ~50ms read timeout below is our timer.
                const int interval = heartbeatIntervalMs_.load();
                if (interval > 0) {
                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastBeat).count() >= interval) {
                        send(heartbeatMessage_);
                        lastBeat = now;
                    }
                }

                DWORD bytes = 0;
                BOOL  ok    = ReadFile(h, buffer, sizeof(buffer), &bytes, nullptr);

                if (!running) { break; }

                if (!ok) {
                    // Read failed — a closed handle (our disconnect) or a lost
                    // device. Treat a lost device as a disconnect.
                    DWORD err = GetLastError();
                    dbg("[SerialClient] ReadFile failed (%lu)", err);
                    fireDisconnect();
                    break;
                }

                if (bytes > 0) {
                    fireData(buffer, (int)bytes);
                }
                // bytes == 0 is a normal read timeout with no data — loop again.
            }
        }
    };
}
