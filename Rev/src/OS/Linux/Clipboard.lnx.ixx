module;

#include <cstdio>
#include <cstdlib>
#include <string>

export module Rev.OS.Clipboard;

export namespace Rev::OS {

    struct Clipboard {

        static bool commandExists(const char* command) {
            std::string test = "command -v ";
            test += command;
            test += " >/dev/null 2>&1";
            return std::system(test.c_str()) == 0;
        }

        // No native clipboard API on X11 without becoming a selection owner
        // and answering SelectionRequest events asynchronously -- shelling
        // out to xclip/xsel (whichever is installed) is the same pragmatic
        // approach already used for zenity-backed dialogs elsewhere.
        static bool SetText(const std::string& text) {
            const char* command = nullptr;
            if (commandExists("xclip")) command = "xclip -selection clipboard";
            else if (commandExists("xsel")) command = "xsel --clipboard --input";
            else {
                std::fprintf(stderr, "[Clipboard] Neither xclip nor xsel found\n");
                return false;
            }

            FILE* pipe = popen(command, "w");
            if (!pipe) return false;

            fwrite(text.data(), 1, text.size(), pipe);
            return pclose(pipe) == 0;
        }
    };
}
