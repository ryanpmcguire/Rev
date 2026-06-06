module;

#include <cstddef>
#include <vector>
#include <cmath>

#include <nlohmann/json.hpp>

export module Cam.App.Probe;

import Rev.Core.Pos3;

// Module-internal alias (NOT exported) so we don't inject a second `Pos3` into
// Cam::App for importers that already have their own.
namespace Cam::App { using Pos3 = Rev::Core::Pos3; }

export namespace Cam::App {

    using Json = nlohmann::json;

    // ==================================================================
    // Probing data model  (the "define a probe from the GUI side" core)
    // ==================================================================
    //
    // A probe is a *sub-component* of a Stage (see Stage::probe): a break from
    // the normal machining path where the machine task-switches to the probe
    // tool, touches a set of part features, and from the measured vs. nominal
    // geometry fits a persistent frame correction that is applied to every
    // subsequent operation until the next probe runs.
    //
    // All target geometry is stored in the MODEL / CAD frame -- exactly the
    // frame toolpath points live in -- so the same UserFrame transform the
    // executor already applies to cut moves also applies to probe moves.

    // One probe target: a point on the part surface and the outward surface
    // normal there.  The machine starts `standoff` mm out along +normal and
    // drives in along -normal; it may travel up to `overtravel` mm past the
    // nominal point before the touch is declared a failure.
    struct ProbeTarget {

        Pos3   point{};            // nominal contact point (CAD frame)
        Pos3   normal{};           // outward unit surface normal (CAD frame)
        double standoff   = 5.0;   // begin this far out along +normal
        double overtravel = 3.0;   // max travel past nominal before "fail"

        // -- Result (transient; filled in after a probing run) ----------
        bool   measured = false;
        Pos3   measuredPoint{};    // actual contact point (CAD frame)

        Json getState() const {
            Json j;
            j["point"]       = Json::array({ point.x,  point.y,  point.z  });
            j["normal"]      = Json::array({ normal.x, normal.y, normal.z });
            j["standoff"]    = standoff;
            j["overtravel"]  = overtravel;
            // Measured contact is a per-run result, not design intent, so it is
            // intentionally NOT serialized -- a loaded project starts un-probed.
            return j;
        }

        void setState(const Json& j) {
            readVec3(j, "point",  point);
            readVec3(j, "normal", normal);
            if (j.contains("standoff")   && j["standoff"].is_number())   { standoff   = j["standoff"].get<double>();   }
            if (j.contains("overtravel") && j["overtravel"].is_number()) { overtravel = j["overtravel"].get<double>(); }
            measured = false;
        }

        static void readVec3(const Json& j, const char* key, Pos3& out) {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                out.x = j[key][0].get<float>();
                out.y = j[key][1].get<float>();
                out.z = j[key][2].get<float>();
            }
        }
    };

    // The fitted rigid result of a probing run: a frame correction applied as
    //   p_corrected = R * p_nominal + t
    // mapping nominal CAD coordinates onto the measured part pose.  Persists
    // (in the executor) until the next probe replaces it.  R is row-major 3x3.
    struct ProbeResult {

        bool   valid = false;
        double r[9]  = { 1, 0, 0,  0, 1, 0,  0, 0, 1 };
        Pos3   t{};
        double rmsError = 0.0;

        void reset() { *this = ProbeResult(); }

        // Apply the correction to a CAD-frame point.
        Pos3 apply(const Pos3& p) const {
            return {
                float(r[0] * p.x + r[1] * p.y + r[2] * p.z + t.x),
                float(r[3] * p.x + r[4] * p.y + r[5] * p.z + t.y),
                float(r[6] * p.x + r[7] * p.y + r[8] * p.z + t.z)
            };
        }

        Json getState() const {
            Json j;
            j["valid"] = valid;
            j["r"]     = Json::array({ r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8] });
            j["t"]     = Json::array({ t.x, t.y, t.z });
            j["rmsError"] = rmsError;
            return j;
        }

        void setState(const Json& j) {
            if (j.contains("valid") && j["valid"].is_boolean()) { valid = j["valid"].get<bool>(); }
            if (j.contains("r") && j["r"].is_array() && j["r"].size() >= 9) {
                for (int i = 0; i < 9; i++) { r[i] = j["r"][i].get<double>(); }
            }
            if (j.contains("t") && j["t"].is_array() && j["t"].size() >= 3) {
                t.x = j["t"][0].get<float>();
                t.y = j["t"][1].get<float>();
                t.z = j["t"][2].get<float>();
            }
            if (j.contains("rmsError") && j["rmsError"].is_number()) { rmsError = j["rmsError"].get<double>(); }
        }
    };

    // The probe sub-component attached to a Stage.  `enabled` marks the stage as
    // carrying a probe step (a break in the machining path before this stage's
    // own cut runs); `targets` are the features to touch; `result` is the most
    // recent fitted correction.
    struct ProbeSpec {

        bool                     enabled = false;
        std::vector<ProbeTarget> targets;
        ProbeResult              result;

        bool   ready()  const { return enabled && targets.size() >= 3; }   // 3+ pts -> full frame
        size_t count()  const { return targets.size(); }

        void clearResult() { result.reset(); for (ProbeTarget& t : targets) { t.measured = false; } }

        Json getState() const {
            Json j;
            j["enabled"] = enabled;
            j["targets"] = Json::array();
            for (const ProbeTarget& t : targets) { j["targets"].push_back(t.getState()); }
            j["result"]  = result.getState();
            return j;
        }

        void setState(const Json& j) {
            targets.clear();
            result.reset();
            if (!j.is_object()) { return; }
            if (j.contains("enabled") && j["enabled"].is_boolean()) { enabled = j["enabled"].get<bool>(); }
            if (j.contains("targets") && j["targets"].is_array()) {
                for (const Json& tj : j["targets"]) {
                    ProbeTarget t;
                    t.setState(tj);
                    targets.push_back(t);
                }
            }
            if (j.contains("result") && j["result"].is_object()) { result.setState(j["result"]); }
        }
    };
}
