module;

#include <cstddef>
#include <vector>

export module Resource;

export struct Resource {

    const unsigned char* data;
    size_t len;
};