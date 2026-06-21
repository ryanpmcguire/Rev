module;

#include <string>
#include <vector>
#include <charconv>

#include <dbg.hpp>

export module Machine.Machines.Carvera.Adapter;

import Rev.Client;

import Machine.Adapter;
import Machine.Events;
import Machine.Command;

export namespace Machine::Carvera {

    // The Carvera adapter: the only place that knows Carvera's wire dialect.
    // TODO(threading): Client callbacks fire on a worker thread; emit() crosses
    // into main-thread state.
    struct Adapter : public Machine::Adapter {

        // Transport (composed; deleted first in dtor, joining its worker)
        Rev::Client* client = nullptr;

        // Target
        std::string host = "192.168.1.104";
        int         port = 2222;

        // Receive buffers
        std::string              rxBuffer;   // raw bytes until a full line arrives
        std::vector<std::string> lines;      // completed lines awaiting processing

        // A decoded line (any subset present)
        struct Inbound {
            bool hasState     = false; Machine::StateEvent     state;
            bool hasTelemetry = false; Machine::TelemetryEvent telemetry;
            bool hasInfo      = false; Machine::InfoEvent      info;
            bool hasLog       = false; Machine::LogEvent       log;
        };

        // Construct/destruct
        //--------------------------------------------------

        Adapter() {

            client = new Rev::Client();

            // Keep-alive: poll "?" every second (also drives the status frame)
            client->setHeartbeat("?", 1000);

            // Transport callbacks -> emit on our channels
            client->onConnect   ([this](Rev::Client::ConnectEvent& e)  { emit(Machine::ConnectionEvent{ Machine::ConnectionStatus::Connected, e.address }); });
            client->onDisconnect([this](Rev::Client::DisconnectEvent&) { emit(Machine::ConnectionEvent{ Machine::ConnectionStatus::Disconnected, "" }); });
            client->onError     ([this](Rev::Client::ErrorEvent& e)    { emit(Machine::ConnectionEvent{ Machine::ConnectionStatus::Error, e.reason }); });
            client->onData      ([this](Rev::Client::DataEvent& e)     { ingest(e.data); });
        }

        ~Adapter() {

            // Transport first -- joins the worker before our buffers go.
            delete client;
        }

        // Lifecycle
        //--------------------------------------------------

        void connect() override {

            emit(Machine::ConnectionEvent{ Machine::ConnectionStatus::Connecting, host });
            client->connect(host, port);
        }

        void disconnect() override { client->disconnect(); }

        // Send -- a command emits its own wire form; we just transport it
        void sendCommand(const Machine::Command& command) override {

            if (!client->isConnected.load()) { return; }
            client->send(command.emit());
        }

        // Decode -- wire line -> structs
        //--------------------------------------------------

        Inbound decode(const std::string& line) const {

            Inbound in;
            if (line.empty()) { return in; }

            // Status frame, e.g. "<Idle|MPos:0.000,0.000,0.000,0.000|...>"
            if (line.front() == '<' && line.back() == '>') {
                dbg("[RawFrame] %s", line.c_str());   // TEMP: measure what the machine sends
                decodeStatus(line, in);
                return in;
            }

            // Info reply, e.g. "[G54:..]", "[TL0:..]", "[PRB:..:1]" (from "$#")
            if (line.front() == '[' && line.back() == ']' && decodeInfo(line, in.info)) {
                in.hasInfo = true;
                return in;
            }

            // Otherwise: back-talk for the log
            in.log = Machine::LogEvent{ line };
            in.hasLog = true;
            return in;
        }

        // Status frame: field 0 is the run-state; the rest are "Prefix:payload"
        // pairs. Fill what is present; unknown prefixes are ignored.
        void decodeStatus(const std::string& frame, Inbound& in) const {

            std::string body = frame.substr(1, frame.size() - 2);
            std::vector<std::string> fields = split(body, '|');
            if (fields.empty()) { return; }

            // Run-state
            in.state = Machine::StateEvent{ fields.front() };
            in.hasState = true;

            // Telemetry pieces
            Machine::TelemetryEvent t;

            for (size_t i = 1; i < fields.size(); ++i) {

                const std::string& field = fields[i];
                size_t colon = field.find(':');
                if (colon == std::string::npos) { continue; }

                std::string key = field.substr(0, colon);
                std::string val = field.substr(colon + 1);

                std::vector<std::string> n = split(val, ',');

                if      (key == "MPos") { fillAxes(val, t.mx, t.my, t.mz, t.ma, t.mb); }
                else if (key == "WPos") { fillAxes(val, t.wx, t.wy, t.wz, t.wa, t.wb); }
                else if (key == "F") {   // current, cap, override%
                    t.feed = nth(n, 0); t.feedTarget = nth(n, 1); t.feedScale = nth(n, 2);
                }
                else if (key == "S") {   // rpm, target, override%, load%, temp
                    t.spindleRpm = nth(n, 0); t.spindleTarget = nth(n, 1); t.spindleScale = nth(n, 2);
                    t.spindleLoad = nth(n, 3); t.spindleTemp = nth(n, 4);
                }
                else if (key == "T") {   // number, length offset
                    t.tool = static_cast<int>(nth(n, 0)); t.toolOffset = nth(n, 1);
                }
                else if (key == "L") {   // ..., power, override%
                    t.laserPower = nth(n, 3); t.laserScale = nth(n, 4);
                }

                // Not yet mapped: W:<v> and C:<...>
            }

            in.telemetry = t;
            in.hasTelemetry = true;
        }

        // Bracketed "$#" reply line: "[G54:x,y,z,a,b]", "[TL0:-0.0016]",
        // "[PRB:x,y,z:triggered]". Returns false for bracketed lines we don't map
        // (modal "[G0 ...]", extended WCS "[G59.1:...]") so they fall to the log.
        bool decodeInfo(const std::string& line, Machine::InfoEvent& out) const {

            std::string body = line.substr(1, line.size() - 2);   // strip [ ]
            size_t colon = body.find(':');
            if (colon == std::string::npos) { return false; }     // e.g. modal "[G0 G54 ...]"

            std::string key  = body.substr(0, colon);
            std::string rest = body.substr(colon + 1);

            using F = Machine::InfoEvent::Field;

            if      (key == "G54") { out.field = F::FrameG54; fillAxes(rest, out.x, out.y, out.z, out.a, out.b); return true; }
            else if (key == "G55") { out.field = F::FrameG55; fillAxes(rest, out.x, out.y, out.z, out.a, out.b); return true; }
            else if (key == "G56") { out.field = F::FrameG56; fillAxes(rest, out.x, out.y, out.z, out.a, out.b); return true; }
            else if (key == "G57") { out.field = F::FrameG57; fillAxes(rest, out.x, out.y, out.z, out.a, out.b); return true; }
            else if (key == "G58") { out.field = F::FrameG58; fillAxes(rest, out.x, out.y, out.z, out.a, out.b); return true; }
            else if (key == "G59") { out.field = F::FrameG59; fillAxes(rest, out.x, out.y, out.z, out.a, out.b); return true; }
            else if (key == "G28") { out.field = F::FrameG28; fillAxes(rest, out.x, out.y, out.z, out.a, out.b); return true; }
            else if (key == "G30") { out.field = F::FrameG30; fillAxes(rest, out.x, out.y, out.z, out.a, out.b); return true; }
            else if (key == "G92") { out.field = F::FrameG92; fillAxes(rest, out.x, out.y, out.z, out.a, out.b); return true; }
            else if (key == "TL0") { out.field = F::ToolLengthOffset; out.tlo = toFloat(rest); return true; }
            else if (key == "PRB") {
                std::vector<std::string> parts = split(rest, ':');   // "x,y,z" : "flag"
                fillAxes(parts.empty() ? "" : parts[0], out.x, out.y, out.z, out.a, out.b);
                out.probeTriggered = parts.size() > 1 && toFloat(parts[1]) != 0.0f;
                out.field = F::Probe;
                return true;
            }

            return false;   // unknown bracketed key (e.g. "G59.1") -> log
        }

        // Parse helpers
        //--------------------------------------------------

        // i-th comma value, or 0 if absent
        static float nth(const std::vector<std::string>& v, size_t i) {
            return i < v.size() ? toFloat(v[i]) : 0.0f;
        }

        // Fill as many axes as the list carries
        static void fillAxes(const std::string& csv, float& x, float& y, float& z, float& a, float& b) {

            std::vector<std::string> n = split(csv, ',');
            if (n.size() > 0) { x = toFloat(n[0]); }
            if (n.size() > 1) { y = toFloat(n[1]); }
            if (n.size() > 2) { z = toFloat(n[2]); }
            if (n.size() > 3) { a = toFloat(n[3]); }
            if (n.size() > 4) { b = toFloat(n[4]); }
        }

        static std::vector<std::string> split(const std::string& s, char sep) {

            std::vector<std::string> out;
            size_t start = 0;

            while (true) {
                size_t at = s.find(sep, start);
                out.push_back(s.substr(start, at - start));
                if (at == std::string::npos) { break; }
                start = at + 1;
            }

            return out;
        }

        static float toFloat(const std::string& s) {

            float value = 0.0f;
            size_t i = 0;
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) { ++i; }   // some fields pad with spaces

            std::from_chars(s.data() + i, s.data() + s.size(), value);
            return value;
        }

        static std::string trimTrailing(std::string s) {
            while (!s.empty() && (s.back() == '\r' || s.back() == ' ')) { s.pop_back(); }
            return s;
        }

        // Framing
        //--------------------------------------------------

        // Carve bytes into whole lines; does not decode
        void ingest(const std::vector<char>& bytes) {

            rxBuffer.append(bytes.data(), bytes.size());

            size_t newline;
            while ((newline = rxBuffer.find('\n')) != std::string::npos) {
                lines.push_back(trimTrailing(rxBuffer.substr(0, newline)));
                rxBuffer.erase(0, newline + 1);
            }

            // Drain now; eventually the main-thread tick calls process()
            process();
        }

        // Decode every buffered line, emit, then clear
        void process() {

            for (const std::string& line : lines) {
                route(decode(line));
            }

            lines.clear();
        }

        // Emit a decoded line's messages on our channels
        void route(const Inbound& in) {
            if (in.hasState)     { emit(in.state); }
            if (in.hasTelemetry) { emit(in.telemetry); }
            if (in.hasInfo)      { emit(in.info); }
            if (in.hasLog)       { emit(in.log); }
        }
    };
}
