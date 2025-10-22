module;

#include <cstddef>
#include <vector>

export module Rev.Core.Resource;

export namespace Rev::Core {
        
    struct Resource {

        const unsigned char* data;
        size_t size;
    };
}