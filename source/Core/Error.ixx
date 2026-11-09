module;
#include <exception>
#include <string>
#include <utility>
#include <vector>

export module Rev.Core.Error;

// Rev::Core::Error: one plain error value, in place of strings. What went
// wrong, who says so, and the errors that caused it. No kinds and no codes:
// a reader reads the tree. Thrown and caught as a std::exception.
export namespace Rev::Core {

    struct Error final : std::exception {
        std::string source;             // who says it ("HomeCamera"); may be empty
        std::string statement;          // what went wrong, one line; empty for a pure group
        std::vector<Error> causes;      // why, each an Error of its own

        Error() = default;
        explicit Error(std::string what, std::string from = {})
            : source(std::move(from)), statement(std::move(what)) {}

        // Anything thrown, as an Error: an Error as it is, a std::exception
        // as a leaf from its what(), anything else as "unknown failure".
        static Error From(std::exception_ptr thrown) {
            try {
                std::rethrow_exception(thrown);
            }
            catch (const Error& error) {
                return error;
            }
            catch (const std::exception& error) {
                return Error(error.what());
            }
            catch (...) {
                return Error("unknown failure");
            }
        }

        // Says it came from `from`, unless it already names its source.
        Error& by(std::string from) {
            if (source.empty()) {
                source = std::move(from);
            }
            return *this;
        }
        // Adds a cause.
        Error& because(Error cause) {
            causes.push_back(std::move(cause));
            return *this;
        }

        // One line: the statement; with none of its own, its causes' lines,
        // the same line once with every source that said it:
        // "Imager is not connected (HomeCamera, HomeProjector)".
        std::string line() const {
            if (!statement.empty() || causes.empty()) {
                return statement;
            }
            std::vector<std::pair<std::string, std::string>> grouped;   // line, sources
            for (const Error& cause : causes) {
                Join(grouped, cause.line(), cause.source);
            }
            std::string out;
            for (const auto& [text, sources] : grouped) {
                out += (out.empty() ? "" : "; ") + text + (sources.empty() ? "" : " (" + sources + ")");
            }
            return out;
        }

        // The whole tree, one indented line per error, "source: statement".
        std::string describe(size_t depth = 0) const {
            std::string out(depth * 2, ' ');
            out += source.empty() ? statement : source + (statement.empty() ? "" : ": " + statement);
            out += "\n";
            for (const Error& cause : causes) {
                out += cause.describe(depth + 1);
            }
            return out;
        }

        const char* what() const noexcept override {
            try {
                summary = line();
            }
            catch (...) {
                return "error";
            }
            return summary.c_str();
        }

    private:
        mutable std::string summary;    // what()'s storage

        static void Join(std::vector<std::pair<std::string, std::string>>& grouped,
            const std::string& text, const std::string& from) {
            for (auto& [line, sources] : grouped) {
                if (line == text) {
                    sources += (sources.empty() || from.empty() ? "" : ", ") + from;
                    return;
                }
            }
            grouped.push_back({text, from});
        }
    };
}
