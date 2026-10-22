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
    // at the event boundary: a worker owns the port and enqueues bytes via
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

            stopRequested_ = true;
            if (worker_.joinable()) {
                CancelSynchronousIo(worker_.native_handle());
                worker_.join();
            }
        }

        // Main-thread facade. Opening, configuring and closing remain worker-owned.
        void connect(std::string port, int baud) override {
            if (running) return;
            {
                std::lock_guard<std::mutex> lock(outgoingMutex_);
                outgoing_.clear();
            }
            pendingPort_ = std::move(port);
            pendingBaud_ = baud;
            pendingConnect_ = true;
            running = true;
            isConnected = false;
            Core::Process::instance().schedule(this, PumpIntervalMs,
                [this](uint64_t) { service(); });
            service();
        }

        // -- API ---------------------------------------------------------

        void send(const std::string& msg) override {
            if (!running) {
                dbg("[SerialClient] send() on closed port");
                return;
            }

            // WriteFile must never run on Rev's main thread. Queue complete
            // messages and let the port-owning worker drain them in order.
            std::lock_guard<std::mutex> lock(outgoingMutex_);
            outgoing_.push_back(msg);
        }

        void setResetOnConnect(bool enabled) {
            resetOnConnect_ = enabled;
        }

        void disconnect() override {
            const bool wasActive = running.exchange(false);
            isConnected = false;
            pendingConnect_ = false;
            stopRequested_ = true;
            discardEvents_ = true;
            // Cancellation does not wait for the driver or close its handle.
            if (worker_.joinable()) CancelSynchronousIo(worker_.native_handle());
            {
                std::lock_guard<std::mutex> lock(outgoingMutex_);
                outgoing_.clear();
            }
            // Keep the pump alive until worker cleanup has completed.
            if (wasActive) {
                DisconnectEvent event{};
                disconnectDispatcher_.tell(&SerialClient::disconnectEvent, event);
            }
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
        std::mutex           outgoingMutex_;
        std::vector<std::string> outgoing_;
        std::atomic<bool> resetOnConnect_ = false;
        std::atomic<bool> stopRequested_ = false;
        std::atomic<bool> finished_ = true;
        bool pendingConnect_ = false, discardEvents_ = false;
        std::string pendingPort_;
        int pendingBaud_ = 115200;

        void service() {
            if (discardEvents_) {
                std::lock_guard<std::mutex> lock(queueMutex_);
                queue_.clear();
            }
            if (finished_) {
                // finished_ is published only after all driver operations end.
                if (worker_.joinable()) worker_.join();
                if (pendingConnect_) {
                    {
                        std::lock_guard<std::mutex> lock(queueMutex_);
                        queue_.clear();
                    }
                    port_ = std::move(pendingPort_);
                    baud_ = pendingBaud_;
                    pendingConnect_ = false;
                    discardEvents_ = false;
                    stopRequested_ = false;
                    finished_ = false;
                    worker_ = std::thread([this]() {
                        workerMain();
                        finished_ = true;
                    });
                } else {
                    running = false;
                }
            }
            if (!discardEvents_) pump();
            if (finished_ && !pendingConnect_ && !running) stopPump();
        }

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
                stopRequested_ = true;
                return;
            }

            // DCB: 115200 8-N-1, NO flow control (matches the stock controller).
            DCB dcb{};
            dcb.DCBlength = sizeof(DCB);
            if (!GetCommState(h, &dcb)) {
                dbg("[SerialClient] GetCommState failed");
                fireError("GetCommState failed");
                CloseHandle(h);
                stopRequested_ = true;
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
                stopRequested_ = true;
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
            timeouts.WriteTotalTimeoutConstant   = 100;
            timeouts.WriteTotalTimeoutMultiplier = 0;
            if (!SetCommTimeouts(h, &timeouts)) {
                fireError("SetCommTimeouts failed");
                CloseHandle(h);
                stopRequested_ = true;
                return;
            }

            handle_.store(h);

            PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
            if (resetOnConnect_) {
                EscapeCommFunction(h, CLRDTR);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                EscapeCommFunction(h, SETDTR);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            else {
                EscapeCommFunction(h, SETDTR);
            }

            if (stopRequested_) {   // disconnected during the ~1s settle
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
            stopRequested_ = true;
        }

        void readLoop(HANDLE h) {

            char buffer[4096];
            auto lastBeat = std::chrono::steady_clock::now();

            while (!stopRequested_) {

                if (!drainOutgoing(h)) { break; }

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

                if (!drainOutgoing(h)) { break; }

                DWORD bytes = 0;
                BOOL  ok    = ReadFile(h, buffer, sizeof(buffer), &bytes, nullptr);

                if (stopRequested_) { break; }

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

        bool drainOutgoing(HANDLE h) {

            std::vector<std::string> batch;
            {
                std::lock_guard<std::mutex> lock(outgoingMutex_);
                batch.swap(outgoing_);
            }

            for (const std::string& message : batch) {
                std::size_t sent = 0;

                while (sent < message.size() && !stopRequested_) {
                    DWORD written = 0;
                    const DWORD remaining = static_cast<DWORD>(message.size() - sent);
                    const BOOL ok = WriteFile(
                        h,
                        message.data() + sent,
                        remaining,
                        &written,
                        nullptr
                    );

                    if (!ok || written == 0) {
                        const DWORD error = GetLastError();
                        dbg(
                            "[SerialClient] Write failed after %zu/%zu bytes (%lu)",
                            sent,
                            message.size(),
                            error
                        );
                        fireError("Serial write failed (" + std::to_string(error) + ")");
                        stopRequested_ = true;
                        return false;
                    }

                    sent += static_cast<std::size_t>(written);
                }
            }

            return !stopRequested_;
        }
    };
}
