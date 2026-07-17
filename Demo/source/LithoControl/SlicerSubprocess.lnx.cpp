module;

#include <string>
#include <functional>
#include <cstdio>

#include <sys/wait.h>

module LithoControl.Interface;   // implementation unit -- no 'export'

namespace LithoControl {

    int Interface::runCapturedProcess(const std::string& cmd, const std::string& cwd,
                                       const std::function<void(const std::string&)>& onLine) {
        // popen() only gives stdout; redirect stderr into it too, matching the
        // Windows path's si.hStdError = hW (both streams merged into one pipe).
        // `cd <cwd> &&` sets the subprocess working directory -- popen() always
        // inherits this process's cwd otherwise, with no separate parameter for it
        // the way CreateProcessA has.
        std::string fullCmd = cwd.empty()
            ? (cmd + " 2>&1")
            : ("cd '" + cwd + "' && " + cmd + " 2>&1");

        FILE* pipe = popen(fullCmd.c_str(), "r");
        if (!pipe) return -1;

        std::string pending;
        char buf[256];
        size_t rd;
        while ((rd = fread(buf, 1, sizeof(buf) - 1, pipe)) > 0) {
            buf[rd] = '\0';
            pending.append(buf, rd);

            size_t pos;
            while ((pos = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, pos);
                pending.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) onLine(line);
            }
        }
        if (!pending.empty()) onLine(pending);

        int status = pclose(pipe);
        if (status < 0) return -1;
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        return 1;   // killed by signal or otherwise didn't exit cleanly
    }

} // namespace LithoControl
