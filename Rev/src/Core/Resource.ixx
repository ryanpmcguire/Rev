module;

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>
#include <filesystem>

export module Rev.Core.Resource;

import Rev.Managed.Files;

export namespace Rev::Core {

    struct Resource {

        const unsigned char* data;
        size_t size;

        bool operator==(const Resource& other) const {
            return data == other.data && size == other.size;
        }

        static Resource FromFile(const std::string& anchor, const std::string& relativePath) 
        {
            Resource resource{};

            //
            // Normalize slashes
            //
            std::string clean = relativePath;
            std::replace(clean.begin(), clean.end(), '\\', '/');

            //
            // Determine resolution mode:
            //  - "./" prefix => anchor-relative
            //  - otherwise    => project-root absolute
            //
            bool isRelative = clean.starts_with("./");

            std::filesystem::path resolvedPath;

            if (isRelative)
            {
                // Remove leading "./"
                std::string stripped = clean.substr(2);

                std::filesystem::path srcFile = std::filesystem::path(anchor).lexically_normal();
                std::filesystem::path srcDir  = srcFile.parent_path();
                resolvedPath = (srcDir / stripped).lexically_normal();
            }
            else
            {
                // Absolute inside project root
                resolvedPath = std::filesystem::path(PROJECT_ROOT) / clean;
                resolvedPath = resolvedPath.lexically_normal();
            }

            //
            // Convert to normalized forward-slash string
            //
            std::string full = resolvedPath.generic_string();

            //
            // Normalize project root
            //
            std::string root = std::string(PROJECT_ROOT);
            std::replace(root.begin(), root.end(), '\\', '/');

            if (!root.empty() && root.back() != '/')
                root += '/';

            //
            // Compute virtual path (full - root)
            //
            std::string virt;

            if (full.starts_with(root)) {
                virt = full.substr(root.size());
            } else {
                virt = full;  // fallback
            }

            //
            // Lookup in atlas
            //
            for (size_t i = 0; i < FilesDB::Count; i++)
            {
                if (FilesDB::Atlas[i].virtualPath == virt) 
                {
                    resource.data = FilesDB::Atlas[i].data;
                    resource.size = FilesDB::Atlas[i].size;
                    return resource;
                }
            }

            // Not found
            resource.data = nullptr;
            resource.size = 0;
            return resource;
        }

    };
}