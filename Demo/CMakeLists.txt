cmake_minimum_required(VERSION 3.10)
project(LithoRev)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API 1)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/source)
set(RCS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/resources)
set(EXT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external)

# Collect sources
file(GLOB_RECURSE CPP_SOURCES CONFIGURE_DEPENDS
    ${SRC_DIR}/*.cpp
    ${EXT_DIR}/*.cpp
    ${RCS_DIR}/.modules/*.cpp
)

# Collect sources
file(GLOB_RECURSE OBJCPP_SOURCES CONFIGURE_DEPENDS
    ${SRC_DIR}/*.mm
    ${EXT_DIR}/*.mm
)

file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS
    ${SRC_DIR}/*.ixx
    ${EXT_DIR}/*.ixx
    ${RCS_DIR}/.modules/*.ixx
)

# Filter sources by platform marker in the filename (e.g. Foo.win.cpp,
# Foo.lnx.cpp) -- same convention/mechanism as Rev/CMakeLists.txt.
#--------------------------------------------------

if(WIN32)
  set(OS_MARKER "win")
elseif(APPLE)
  set(OS_MARKER "mac")
elseif(UNIX)
  set(OS_MARKER "lnx")
endif()

function(filter_sources out_var)
    set(result "")
    foreach(src ${ARGN})
        get_filename_component(fname ${src} NAME)

        string(REGEX MATCHALL "\\.([^.]+)\\." markers ${fname})

        set(ok TRUE)
        foreach(m ${markers})
            string(REPLACE "." "" m ${m})
            string(TOLOWER "${m}" m)

            if(m STREQUAL "${OS_MARKER}")
                # allowed, matches current OS
            elseif(m IN_LIST VALID_OS_MARKERS)
                # mismatched marker -> exclude
                set(ok FALSE)
            endif()
        endforeach()

        if(ok)
            list(APPEND result ${src})
        endif()
    endforeach()

    set(${out_var} "${result}" PARENT_SCOPE)
endfunction()

set(VALID_OS_MARKERS win mac osx ios lnx linux unix android)

filter_sources(CPP_SOURCES_FILTERED ${CPP_SOURCES})
filter_sources(MODULE_SOURCES_FILTERED ${MODULE_SOURCES})

set(CPP_SOURCES ${CPP_SOURCES_FILTERED})
set(MODULE_SOURCES ${MODULE_SOURCES_FILTERED})

# Windows resource files (icon, version info)
if(WIN32)
    file(GLOB RC_SOURCES CONFIGURE_DEPENDS ${RCS_DIR}/*.rc)
endif()

# Add executable
add_executable(LithoRev)

# Set target properties
set_target_properties(LithoRev PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  UNITY_BUILD OFF
  CXX_SCAN_FOR_MODULES ON
)

# Add implementation sources
target_sources(LithoRev
    PUBLIC ${CPP_SOURCES} ${OBJCPP_SOURCES}
)

# Add module interface sources
target_sources(LithoRev
    PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES ${MODULE_SOURCES}
)

# Add Windows resources (RC files are compiled by MSVC rc.exe automatically)
if(RC_SOURCES)
    target_sources(LithoRev PRIVATE ${RC_SOURCES})
endif()

target_link_libraries(LithoRev PRIVATE Rev)
if(WIN32)
    target_link_libraries(LithoRev PRIVATE shell32)
endif()

# Build as a GUI app (no console window) for all non-Debug configs.
# Keep the standard main() entry point via mainCRTStartup. Debug stays a
# console app so dbg.hpp output remains visible.
if (WIN32 AND MSVC)
    target_link_options(LithoRev PRIVATE
        $<$<NOT:$<CONFIG:Debug>>:/SUBSYSTEM:WINDOWS>
        $<$<NOT:$<CONFIG:Debug>>:/ENTRY:mainCRTStartup>
    )
endif()

target_compile_definitions(LithoRev PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

target_compile_definitions(Rev PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
)
