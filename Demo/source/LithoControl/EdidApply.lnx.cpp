module;

#include <string>

module LithoControl.Interface;   // implementation unit -- no 'export'

namespace LithoControl {

    // No Linux equivalent to a CRU-based Windows EDID override + display driver
    // restart -- a custom projector mode on Linux goes through XRandR instead
    // (see Rev::OS::Display::SetMode), which needs the mode to already exist on
    // the output (or be added via cvt/xrandr --newmode, out of scope here). This
    // whole feature is Windows-only by nature, not a missing port.
    void Interface::runEdidApplySubprocess(const std::string&) {
        logQ.push("[ERR] EDID override is Windows-only. On Linux, use xrandr "
                   "to select or add a custom mode for the projector output "
                   "(see Rev::OS::Display).");
        pendingEdidDone = true;
    }

} // namespace LithoControl
