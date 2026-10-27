module;

#include <algorithm>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>

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

        // Hold a modem-control line at a stable level for devices whose USB
        // CDC implementation requires carrier assertion. This is configured
        // once with the DCB; it never performs a reset pulse.
        void setDtrEnabled(bool enabled) { dtrEnabled_ = enabled; }

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

        // Enumerate only USB serial ports matching a device family. Protocol
        // clients must not probe unrelated peripherals at arbitrary baud rates.
        static std::vector<std::string> enumerateUsbPorts(uint16_t vid, uint16_t pid) {
            std::vector<std::string> ports;
            char wanted[32];
            std::snprintf(wanted, sizeof(wanted), "VID_%04X&PID_%04X", vid, pid);
            HKEY usb;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                    "SYSTEM\\CurrentControlSet\\Enum\\USB", 0, KEY_READ, &usb) != ERROR_SUCCESS)
                return ports;
            char device[256]; DWORD deviceIndex = 0;
            while (true) {
                DWORD deviceLength = sizeof(device);
                const LONG deviceResult = RegEnumKeyExA(usb, deviceIndex++, device,
                    &deviceLength, nullptr, nullptr, nullptr, nullptr);
                if (deviceResult == ERROR_NO_MORE_ITEMS) break;
                if (deviceResult != ERROR_SUCCESS ||
                    _strnicmp(device, wanted, std::strlen(wanted)) != 0) continue;
                HKEY deviceKey;
                if (RegOpenKeyExA(usb, device, 0, KEY_READ, &deviceKey) != ERROR_SUCCESS) continue;
                char instance[256]; DWORD instanceIndex = 0;
                while (true) {
                    DWORD instanceLength = sizeof(instance);
                    const LONG instanceResult = RegEnumKeyExA(deviceKey, instanceIndex++, instance,
                        &instanceLength, nullptr, nullptr, nullptr, nullptr);
                    if (instanceResult == ERROR_NO_MORE_ITEMS) break;
                    if (instanceResult != ERROR_SUCCESS) continue;
                    const std::string parameters = std::string(instance) + "\\Device Parameters";
                    HKEY parameterKey;
                    if (RegOpenKeyExA(deviceKey, parameters.c_str(), 0, KEY_READ,
                            &parameterKey) != ERROR_SUCCESS) continue;
                    char port[64]; DWORD portLength = sizeof(port), type = 0;
                    if (RegQueryValueExA(parameterKey, "PortName", nullptr, &type,
                            reinterpret_cast<BYTE*>(port), &portLength) == ERROR_SUCCESS &&
                        type == REG_SZ && portLength > 1)
                        ports.emplace_back(port);
                    RegCloseKey(parameterKey);
                }
                RegCloseKey(deviceKey);
            }
            RegCloseKey(usb);
            const auto active = enumeratePorts();
            std::erase_if(ports, [&](const std::string& port) {
                return std::find(active.begin(), active.end(), port) == active.end();
            });
            std::sort(ports.begin(), ports.end());
            ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
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
        std::optional<bool> dtrEnabled_;
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
            // Preserve DTR unless this specific client requires a stable CDC
            // carrier assertion. Never pulse DTR/RTS during discovery.
            if (dtrEnabled_)
                dcb.fDtrControl = *dtrEnabled_ ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
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
            if (stopRequested_) {
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
                // Bound each write batch so incoming replies are serviced under load.
                std::size_t count = 0, bytes = 0;
                while (count < outgoing_.size() && bytes < 256U) {
                    bytes += outgoing_[count].size();
                    batch.push_back(std::move(outgoing_[count++]));
                }
                outgoing_.erase(outgoing_.begin(), outgoing_.begin() + count);
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
                        if (!stopRequested_) {
                            isConnected = false;
                            fireError("Serial write failed (" + std::to_string(error) + ")");
                            fireDisconnect();
                        }
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
