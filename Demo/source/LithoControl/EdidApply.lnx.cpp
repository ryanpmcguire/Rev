module;

#include <cstdlib>
#include <string>

module LithoControl.Interface;   // implementation unit -- no 'export'

namespace LithoControl {

    // There's no CRU-equivalent EDID registry hack on Linux, and none is needed:
    // unlike the Windows GPU driver, xrandr can add and activate a custom mode on
    // an output without the EDID advertising it first, so this reproduces the
    // *effect* of the Windows override (not the mechanism) via
    // `xrandr --newmode` / `--addmode` / `--output --mode`. The timing values
    // below were decoded byte-for-byte from the checked-in
    // "working custom resolution.bin" (the CRU-exported EDID the Windows path
    // installs), so the resulting mode is pixel-identical: 640x360 @ 60.00 Hz,
    // 18.00 MHz pixel clock, +hsync -vsync. Reuses whatever output is already
    // selected in the HDMI dropdown (same id SetMode() below uses).
    void Interface::runEdidApplySubprocess(const std::string&) {
        if (!hdmiDisplayDrop || hdmiDisplayDrop->params.value.empty()) {
            logQ.push("[ERR] No HDMI display selected -- pick one in the HDMI "
                       "dropdown first, then try APPLY EDID again.");
            pendingEdidDone = true;
            return;
        }

        std::string output = hdmiDisplayDrop->params.value;
        const std::string modeName = "640x360_60.00";
        const std::string modeline = "18.00 640 688 720 800 360 363 368 375 +hsync -vsync";

        if (std::system("command -v xrandr >/dev/null 2>&1") != 0) {
            logQ.push("[ERR] xrandr not found on PATH -- install x11-xserver-utils.");
            pendingEdidDone = true;
            return;
        }

        // --newmode fails harmlessly (xrandr just complains to stderr) if this
        // mode was already added by a previous run -- not treated as fatal.
        // Only the final --output --mode selection determines success/failure.
        std::system(("xrandr --newmode '" + modeName + "' " + modeline + " >/dev/null 2>&1").c_str());
        std::system(("xrandr --addmode '" + output + "' '" + modeName + "' >/dev/null 2>&1").c_str());

        int rc = std::system(("xrandr --output '" + output + "' --mode '" + modeName + "'").c_str());
        if (rc == 0) {
            logQ.push("[OK] Custom 640x360@60 mode applied to " + output + " via xrandr.");
        } else {
            logQ.push("[ERR] xrandr --output '" + output + "' --mode '" + modeName +
                       "' failed (exit " + std::to_string(rc) + "). Run `xrandr --query` "
                       "to confirm the output name, and check the mode wasn't rejected "
                       "by the driver.");
        }

        pendingEdidDone = true;
    }

} // namespace LithoControl
