module;

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#ifdef _WIN32
#include <windows.h>
#endif

export module Rev.Core.Resource;

export namespace Rev::Core {

    struct Resource {

        const unsigned char* data;
        size_t size;

        bool operator==(const Resource& other) const {
            return data == other.data && size == other.size;
        }

        // clever inlines resource bytes at transpile time and rewrites
        // File("./x") -> Resource::FromString(<bytes literal>, <size>). The
        // literal has static storage, so the pointer stays valid for the
        // program's lifetime. (The CMake build still uses FromFile + the atlas.)
        static Resource FromString(const char* data, size_t size)
        {
            return Resource{ reinterpret_cast<const unsigned char*>(data), size };
        }

        // Read a resource straight from disk at runtime.
        //  - "./" prefix  => resolved relative to `anchor` (the calling source file)
        //  - otherwise    => resolved relative to PROJECT_ROOT
        // The bytes live in a program-lifetime cache; the returned Resource is a
        // NON-OWNING view into that cache -- the same contract as an embedded
        // resource (stable pointer for the program's life). The file is re-read
        // when its on-disk timestamp changes, which is the hook for hot-reload
        // (re-call FromFile to pick up edits; previously returned views to that
        // path become stale after a reload).
        static Resource FromFile(const std::string& anchor, const std::string& relativePath)
        {
            namespace fs = std::filesystem;

            std::string clean = relativePath;
            std::replace(clean.begin(), clean.end(), '\\', '/');

            fs::path resolved;
            if (clean.starts_with("./"))
            {
                fs::path srcDir = fs::path(anchor).lexically_normal().parent_path();
                resolved = (srcDir / clean.substr(2)).lexically_normal();
            }
            else
            {
                resolved = (fs::path(PROJECT_ROOT) / clean).lexically_normal();
            }

            const std::string key = resolved.generic_string();

            // Program-lifetime byte cache + last-seen timestamps, keyed by path.
            static std::unordered_map<std::string, std::vector<unsigned char>> cache;
            static std::unordered_map<std::string, fs::file_time_type> stamps;

            std::error_code ec;
            fs::file_time_type mtime = fs::last_write_time(resolved, ec);

            auto it = cache.find(key);
            const bool missing = (it == cache.end());
            const bool changed  = (!ec && !missing && stamps[key] != mtime);
            if (missing || changed)
            {
                std::vector<unsigned char> bytes;
                // Read via the OS API (kernel32), NOT <fstream> -- <fstream>
                // drags in the C stdio + locale/numeric facets (fopen, nan,
                // isalnum, ...), pulling a whole UCRT surface the rest of this
                // build never references. Reading through Win32 keeps the link
                // exactly as it was before resources moved to disk.
#ifdef _WIN32
                HANDLE h = CreateFileW(resolved.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (h != INVALID_HANDLE_VALUE)
                {
                    LARGE_INTEGER sz{};
                    if (GetFileSizeEx(h, &sz) && sz.QuadPart > 0)
                    {
                        bytes.resize(static_cast<size_t>(sz.QuadPart));
                        size_t off = 0;
                        while (off < bytes.size())
                        {
                            DWORD chunk = static_cast<DWORD>(
                                std::min<size_t>(bytes.size() - off, 0x10000000));
                            DWORD got = 0;
                            if (!ReadFile(h, bytes.data() + off, chunk, &got, nullptr) || got == 0)
                                break;
                            off += got;
                        }
                        bytes.resize(off);
                    }
                    CloseHandle(h);
                }
#endif
                cache[key] = std::move(bytes);
                if (!ec) stamps[key] = mtime;
                it = cache.find(key);
            }

            return Resource{ it->second.data(), it->second.size() };
        }

    };
}