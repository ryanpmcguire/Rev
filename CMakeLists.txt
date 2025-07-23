cmake_minimum_required(VERSION 3.28)  # 4.0 is OK too

project(HelloWorld LANGUAGES CXX OBJCXX)

set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API 1)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Detect SDK root for non-AppleClang (e.g., Homebrew Clang)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND NOT "${CMAKE_CXX_COMPILER}" MATCHES "AppleClang")
    message(STATUS "Using non-Apple Clang — locating macOS SDK")

    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE SDK_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    message(STATUS "macOS SDK path: ${SDK_PATH}")

    add_compile_options(-isysroot ${SDK_PATH})
    add_link_options(-isysroot ${SDK_PATH})
endif()


# Define directories
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(EXT_DIR ${CMAKE_SOURCE_DIR}/external)
set(RCS_DIR ${CMAKE_SOURCE_DIR}/resources)

# Embed resources
add_custom_command(
    OUTPUT ${RCS_DIR}/.modules/timestamp
    COMMAND ${CMAKE_COMMAND} -E echo "Running resource embed script..."
    COMMAND ${CMAKE_COMMAND} -E env bash ${CMAKE_SOURCE_DIR}/scripts/Create_Resource_Modules.sh
    COMMAND ${CMAKE_COMMAND} -E touch ${RCS_DIR}/.modules/timestamp
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_custom_target(EmbedResources ALL DEPENDS ${RCS_DIR}/.modules/timestamp)

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

# Add executable
add_executable(${PROJECT_NAME})

# Add implementation sources
target_sources(${PROJECT_NAME}
    PUBLIC ${CPP_SOURCES} ${OBJCPP_SOURCES}
)

# Add module interface sources
target_sources(${PROJECT_NAME}
    PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES ${MODULE_SOURCES}
)

# Link dependencies
target_include_directories(${PROJECT_NAME} PUBLIC ${EXT_DIR} ${SRC_DIR} ${EXT_DIR}/metal-cpp)
target_link_libraries(${PROJECT_NAME} PUBLIC
    ${EXT_DIR}/GLFW/bin/macos/x86_64/libglfw3.a
)

if(APPLE)
    find_library(COCOA_LIBRARY Cocoa REQUIRED)
    find_library(IOKIT_LIBRARY IOKit REQUIRED)
    find_library(CORE_FOUNDATION_LIBRARY CoreFoundation REQUIRED)
    find_library(CORE_VIDEO_LIBRARY CoreVideo REQUIRED)
    find_library(QUARTZCORE_LIBRARY QuartzCore REQUIRED)
    find_library(METAL_LIBRARY Metal REQUIRED)

    target_link_libraries(HelloWorld PRIVATE
        ${COCOA_LIBRARY}
        ${IOKIT_LIBRARY}
        ${CORE_FOUNDATION_LIBRARY}
        ${CORE_VIDEO_LIBRARY}
        ${QUARTZCORE_LIBRARY}
        ${METAL_LIBRARY}
    )
endif()


# Dependency
add_dependencies(${PROJECT_NAME} EmbedResources)
