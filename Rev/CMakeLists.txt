cmake_minimum_required(VERSION 3.26)
project(RevFramework)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API 1)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(RCS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/resources)
set(EXT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external)

# Embed resources
add_custom_command(
    OUTPUT ${RCS_DIR}/.modules/timestamp
    COMMAND ${CMAKE_COMMAND} -E echo "Running resource embed script..."
    COMMAND ${CMAKE_COMMAND} -E env bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/Create_Resource_Modules.sh
    COMMAND ${CMAKE_COMMAND} -E touch ${RCS_DIR}/.modules/timestamp
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Embedding resources with embed.sh"
)

add_custom_target(EmbedResources ALL DEPENDS ${RCS_DIR}/.modules/timestamp)

# Collect sources
#--------------------------------------------------

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

# Filter sources
#--------------------------------------------------

# Detect current OS marker
if(WIN32)
  set(OS_MARKER "win")
  set(PLATFORM "OPENGL")
elseif(APPLE)
  set(OS_MARKER "mac")
  set(PLATFORM "METAL")
elseif(UNIX)
  set(OS_MARKER "lnx")
  set(PLATFORM "OPENGL")
endif()

# Detect current graphics platform marker (normalize to lowercase)
string(TOLOWER "${PLATFORM}" PLATFORM_MARKER)

function(filter_sources out_var)
    set(result "")
    foreach(src ${ARGN})
        get_filename_component(fname ${src} NAME)

        # Collect markers from filename
        string(REGEX MATCHALL "\\.([^.]+)\\." markers ${fname})

        set(ok TRUE)
        foreach(m ${markers})
            string(REPLACE "." "" m ${m}) # strip dots
            string(TOLOWER "${m}" m)

            if(m STREQUAL "${OS_MARKER}" OR m STREQUAL "${PLATFORM_MARKER}")
                # allowed, matches current OS or platform
            elseif(m IN_LIST VALID_OS_MARKERS OR m IN_LIST VALID_PLATFORM_MARKERS)
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
set(VALID_PLATFORM_MARKERS vulkan metal opengl)

filter_sources(CPP_SOURCES_FILTERED ${CPP_SOURCES})
filter_sources(OBJCPP_SOURCES_FILTERED ${OBJCPP_SOURCES})
filter_sources(MODULE_SOURCES_FILTERED ${MODULE_SOURCES})

set(CPP_SOURCES ${CPP_SOURCES_FILTERED})
set(OBJCPP_SOURCES ${OBJCPP_SOURCES_FILTERED})
set(MODULE_SOURCES ${MODULE_SOURCES_FILTERED})

# Add executable
add_library(Rev STATIC)

# Define macro for the chosen platform
target_compile_definitions(Rev PUBLIC PLATFORM_${PLATFORM})

# Add module interface sources (modules must be compiled first)
target_sources(Rev
    PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES ${MODULE_SOURCES}
)

# Add implementation sources
target_sources(Rev
    PUBLIC ${CPP_SOURCES} ${OBJCPP_SOURCES}
)

add_dependencies(Rev EmbedResources)
find_package(OpenGL REQUIRED)

target_include_directories(Rev PUBLIC
    ${SRC_DIR}
    ${RCS_DIR}/.modules
    ${EXT_DIR}
)

target_compile_definitions(Rev PUBLIC GLEW_STATIC)

# Link
#--------------------------------------------------

if (WIN32)
  target_link_libraries(Rev PUBLIC
      ${EXT_DIR}/glew/bin/win/x64/glew32s.lib
      ${EXT_DIR}/freetype/bin/win/x64/freetype.lib
      OpenGL::GL
  )
elseif (APPLE)
    target_link_libraries( Rev PUBLIC
        ${CMAKE_SOURCE_DIR}/Rev/external/glfw/bin/macos/universal/libglfw3.a
        ${CMAKE_SOURCE_DIR}/Rev/external/freetype/bin/mac/x64/libfreetype.a

        # Add required system libs
        -lbz2
        -lpng
        -lz

        # macOS frameworks
        "-framework Cocoa"
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework Carbon"
        "-framework OpenGL"
)

endif()