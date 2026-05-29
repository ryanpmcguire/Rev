option(ENABLE_CCACHE "Use sccache/ccache to speed up recompilation" ON)

if(NOT ENABLE_CCACHE)
    return()
endif()

find_program(SCCACHE_PROGRAM
    NAMES sccache sccache.exe
    HINTS
        "${CMAKE_SOURCE_DIR}/tools/sccache/sccache-v0.15.0-x86_64-pc-windows-msvc"
        "${CMAKE_SOURCE_DIR}/tools/sccache"
)

find_program(CCACHE_PROGRAM
    NAMES ccache ccache.exe
    HINTS
        "${CMAKE_SOURCE_DIR}/tools/ccache/ccache-4.13.6-windows-x86_64"
        "${CMAKE_SOURCE_DIR}/tools/ccache"
)

if(SCCACHE_PROGRAM)
    set(COMPILER_CACHE_PROGRAM "${SCCACHE_PROGRAM}")
    set(COMPILER_CACHE_NAME "sccache")
elseif(CCACHE_PROGRAM)
    set(COMPILER_CACHE_PROGRAM "${CCACHE_PROGRAM}")
    set(COMPILER_CACHE_NAME "ccache")
else()
    message(STATUS "No compiler cache found; run tools/ensure-compiler-cache.ps1 or set ENABLE_CCACHE=OFF")
    return()
endif()

set(CMAKE_C_COMPILER_LAUNCHER "${COMPILER_CACHE_PROGRAM}" CACHE STRING "C compiler launcher" FORCE)
set(CMAKE_CXX_COMPILER_LAUNCHER "${COMPILER_CACHE_PROGRAM}" CACHE STRING "C++ compiler launcher" FORCE)

if(SCCACHE_PROGRAM)
    if(NOT DEFINED ENV{SCCACHE_DIR})
        set(ENV{SCCACHE_DIR} "$ENV{LOCALAPPDATA}/sccache")
    endif()
else()
    if(NOT DEFINED ENV{CCACHE_BASEDIR})
        set(ENV{CCACHE_BASEDIR} "${CMAKE_SOURCE_DIR}")
    endif()

    if(NOT DEFINED ENV{CCACHE_SLOPPINESS})
        set(ENV{CCACHE_SLOPPINESS} "pch_defines,time_macros,include_file_mtime,include_file_ctime")
    endif()
endif()

message(STATUS "Using ${COMPILER_CACHE_NAME}: ${COMPILER_CACHE_PROGRAM}")
