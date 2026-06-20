module;

#include <string>
#include <vector>
#include <charconv>

export module Machine.App.Machines.Carvera.Adapter;

import Rev.Client;

import Machine.App.Adapter;
import Machine.App.Events;
import Machine.App.Command;

export namespace App::Carvera {

    // The Carvera adapter: the only place that knows Carvera's wire dialect.
    // Transport is composed (a Client), not inherited, so it can be swapped (TCP
    // today, USB/serial later). Base names are qualified App:: to avoid colliding
    // with this derived Adapter.
    //
    // THREADING (known gap): Client callbacks arrive on a worker thread, so emit()
    // crosses into main-thread state. The fix is to have the main-thread tick call
    // process() while the worker only fills `lines`. Not yet done.
    struct Adapter : public App::Adapter {

        Rev::Client client;        // the transport (composed, never subclassed)

        std::string host = "192.168.1.1";
        int         port = 2222;

        std::string              rxBuffer;   // raw bytes until a full line arrives
        std::vector<std::string> lines;      // completed lines awaiting processing

        // A decoded line: any subset may be present.
        struct Inbound {
            bool                hasState = false;     App::StateEvent     state;
            bool                hasTelemetry = false; App::TelemetryEvent telemetry;
            bool                hasLog = false;       App::LogEvent       log;
        };

        Adapter() {
            wireTransport();
        }

        ~Adapter() {}

        // Lifecycle
        //--------------------------------------------------

        void connect() override {
            emit(App::ConnectionEvent{ App::ConnectionStatus::Connecting, host });
            client.connect(host, port);
        }

        void disconnect() override {
            client.disconnect();
        }

        void sendCommand(const App::Command& command) override {
            if (!client.isConnected.load()) { return; }
            client.send(encode(command));
        }

        // Translate -- Command -> wire, line -> structs
        //--------------------------------------------------

        std::string encode(const App::Command& command) const {
            switch (command.type) {
                case App::Command::Type::Unlock:      return "$X\n";
                case App::Command::Type::Reset:       return std::string(1, '\x18');  // ctrl-x soft reset
                case App::Command::Type::QueryStatus: return "?";
            }
            return "";
        }

        Inbound decode(const std::string& line) const {

            Inbound in;
            if (line.empty()) { return in; }

            // Status frame, e.g. "<Idle|MPos:0.000,0.000,0.000,0.000|...>".
            if (line.front() == '<' && line.back() == '>') {
                decodeStatus(line, in);
                return in;
            }

            // Otherwise: back-talk for the log.
            in.log = App::LogEvent{ line };
            in.hasLog = true;
            return in;
        }

        void decodeStatus(const std::string& frame, Inbound& in) const {

            std::string body = frame.substr(1, frame.size() - 2);   // strip < >
            std::vector<std::string> fields = split(body, '|');
            if (fields.empty()) { return; }

            in.state = App::StateEvent{ fields.front() };
            in.hasState = true;

            for (const std::string& field : fields) {
                if (field.rfind("MPos:", 0) == 0) {
                    std::vector<std::string> n = split(field.substr(5), ',');
                    App::TelemetryEvent t;
                    if (n.size() > 0) { t.x = toFloat(n[0]); }
                    if (n.size() > 1) { t.y = toFloat(n[1]); }
                    if (n.size() > 2) { t.z = toFloat(n[2]); }
                    if (n.size() > 3) { t.a = toFloat(n[3]); }
                    in.telemetry = t;
                    in.hasTelemetry = true;
                    break;
                }
            }
        }

        // Transport wiring + framing
        //--------------------------------------------------

        void wireTransport() {

            client.onConnect([this](Rev::Client::ConnectEvent& e) {
                emit(App::ConnectionEvent{ App::ConnectionStatus::Connected, e.address });
            });

            client.onDisconnect([this](Rev::Client::DisconnectEvent&) {
                emit(App::ConnectionEvent{ App::ConnectionStatus::Disconnected, "" });
            });

            client.onError([this](Rev::Client::ErrorEvent& e) {
                emit(App::ConnectionEvent{ App::ConnectionStatus::Error, e.reason });
            });

            client.onData([this](Rev::Client::DataEvent& e) {
                ingest(e.data);
            });
        }

        // Framing only -- carve bytes into whole lines; does not decode.
        void ingest(const std::vector<char>& bytes) {

            rxBuffer.append(bytes.data(), bytes.size());

            size_t newline;
            while ((newline = rxBuffer.find('\n')) != std::string::npos) {
                lines.push_back(trimTrailing(rxBuffer.substr(0, newline)));
                rxBuffer.erase(0, newline + 1);
            }

            // Drain immediately for now; the main-thread tick should call process().
            process();
        }

        // Decode every buffered line and emit the results, then clear the buffer.
        void process() {

            for (const std::string& line : lines) {
                route(decode(line));
            }

            lines.clear();
        }

        // Emit a decoded line's messages on the adapter's channels.
        void route(const Inbound& in) {
            if (in.hasState)     { emit(in.state); }
            if (in.hasTelemetry) { emit(in.telemetry); }
            if (in.hasLog)       { emit(in.log); }
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
            std::from_chars(s.data(), s.data() + s.size(), value);
            return value;
        }

        static std::string trimTrailing(std::string s) {
            while (!s.empty() && (s.back() == '\r' || s.back() == ' ')) { s.pop_back(); }
            return s;
        }
    };
}
