#pragma once

#include <cstdlib>
#include <string>

// Shared by the Linux OS modules that shell out to desktop-integration
// binaries (Clipboard -> xclip/xsel, Dialog -> zenity). Plain header, not a
// module, since both callers are separate module interfaces with no shared
// internal module to place this in.
namespace Rev::OS::detail {

    inline bool commandExists(const char* command) {
        std::string test = "command -v ";
        test += command;
        test += " >/dev/null 2>&1";
        return std::system(test.c_str()) == 0;
    }

}
