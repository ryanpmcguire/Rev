module;

#include <initializer_list>
#include <string>

#include <nlohmann/json.hpp>

export module Rev.OS.File.Json;

export import Rev.OS.File;

export namespace Rev::OS {

    using Json = nlohmann::json;

    struct JsonFile : public File {

        // State
        //--------------------------------------------------

        Json json;

        // Create
        //--------------------------------------------------

        JsonFile() {}

        JsonFile(const File& file) : File(file) {}

        JsonFile(std::initializer_list<File::PathComponent> pathComponents)
            : File(pathComponents) {}

        static JsonFile FromPath(
            std::initializer_list<File::PathComponent> pathComponents
        ) {
            return JsonFile(File::FromPath(pathComponents));
        }

        // Load/Save
        //--------------------------------------------------

        bool load() {

            std::string text;

            if (!readText(text) || text.empty()) {
                return false;
            }

            try {
                json = Json::parse(text);
            }
            catch (...) {
                return false;
            }

            return true;
        }

        bool save(
            int indent = 4
        ) const {

            return writeText(json.dump(indent));
        }
    };
}
