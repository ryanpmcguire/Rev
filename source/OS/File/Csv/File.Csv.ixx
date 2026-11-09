module;

#include <charconv>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <system_error>
#include <vector>

export module Rev.OS.File.Csv;

export import Rev.OS.File;

export namespace Rev::OS {

    // A CSV file (RFC 4180): a header of column names and rows of cells.
    struct CsvFile : public File {

        using Row = std::vector<std::string>;

        // State
        //--------------------------------------------------

        Row columns;              // column names (the header)
        std::vector<Row> rows;    // rows held in memory

        // Create
        //--------------------------------------------------

        CsvFile() {}

        CsvFile(const File& file) : File(file) {}

        CsvFile(std::initializer_list<File::PathComponent> pathComponents)
            : File(pathComponents) {}

        static CsvFile FromPath(
            std::initializer_list<File::PathComponent> pathComponents
        ) {
            return CsvFile(File::FromPath(pathComponents));
        }

        // A number as a cell, in the shortest text that round-trips exactly.
        static std::string Cell(double value) {
            char buffer[32];
            std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), value);
            return std::string(buffer, result.ptr);
        }

        // Fill
        //--------------------------------------------------

        void append(const Row& row) {
            rows.push_back(row);
        }

        // Load/Save
        //--------------------------------------------------

        // Parse the whole file: first record is the header, the rest are rows.
        bool read() {

            columns.clear();
            rows.clear();

            std::string text;

            if (!readText(text)) {
                return false;
            }

            std::vector<Row> records = parse(text);

            if (records.empty()) {
                return true;
            }

            columns = records.front();
            rows.assign(records.begin() + 1, records.end());

            return true;
        }

        // Write header and all rows, atomically (temp file, then rename).
        bool write() const {

            if (!valid || path.empty() || !ensureParentDirectoryExists()) {
                return false;
            }

            std::filesystem::path temp = path;
            temp += ".tmp";

            {
                std::ofstream stream(temp, std::ios::binary | std::ios::trunc);

                if (!stream) {
                    return false;
                }

                stream << format(columns);

                for (const Row& row : rows) {
                    stream << format(row);
                }

                if (!stream.good()) {
                    return false;
                }
            }

            std::error_code error;
            std::filesystem::rename(temp, path, error);

            return !error;
        }

        // Append the held rows to the file on disk, writing the header only
        // when the file is new or empty. The appended rows are then let go.
        bool appendToDisk() {

            if (!valid || path.empty() || !ensureParentDirectoryExists()) {
                return false;
            }

            std::error_code error;
            bool fresh = !std::filesystem::exists(path, error) || std::filesystem::file_size(path, error) == 0;

            std::ofstream stream(path, std::ios::binary | std::ios::app);

            if (!stream) {
                return false;
            }

            if (fresh) {
                stream << format(columns);
            }

            for (const Row& row : rows) {
                stream << format(row);
            }

            if (!stream.good()) {
                return false;
            }

            rows.clear();

            return true;
        }

        // Text
        //--------------------------------------------------

        // One record as a CRLF-terminated line, quoting cells that need it.
        static std::string format(const Row& row) {

            std::string line;

            for (size_t i = 0; i < row.size(); ++i) {

                if (i > 0) {
                    line += ',';
                }

                const std::string& cell = row[i];

                if (cell.find_first_of(",\"\r\n") == std::string::npos) {
                    line += cell;
                    continue;
                }

                line += '"';

                for (char c : cell) {
                    if (c == '"') {
                        line += '"';
                    }
                    line += c;
                }

                line += '"';
            }

            line += "\r\n";

            return line;
        }

        // All records of a CSV text, honouring quotes (embedded commas,
        // doubled quotes, line breaks). Accepts CRLF or LF line endings.
        static std::vector<Row> parse(const std::string& text) {

            std::vector<Row> records;
            Row record;
            std::string cell;
            bool quoted = false;
            bool started = false;

            for (size_t i = 0; i < text.size(); ++i) {

                char c = text[i];

                if (quoted) {
                    if (c != '"') {
                        cell += c;
                    }
                    else if (i + 1 < text.size() && text[i + 1] == '"') {
                        cell += '"';
                        ++i;
                    }
                    else {
                        quoted = false;
                    }
                    continue;
                }

                if (c == '"') {
                    quoted = true;
                    started = true;
                }
                else if (c == ',') {
                    record.push_back(cell);
                    cell.clear();
                    started = true;
                }
                else if (c == '\r') {
                    // Line ending is decided by '\n'.
                }
                else if (c == '\n') {
                    record.push_back(cell);
                    records.push_back(record);
                    record.clear();
                    cell.clear();
                    started = false;
                }
                else {
                    cell += c;
                    started = true;
                }
            }

            if (started) {
                record.push_back(cell);
                records.push_back(record);
            }

            return records;
        }
    };
}
