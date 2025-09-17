cmake_minimum_required(VERSION 3.10)
project(RevFramework)

set(CMAKE_CXX_STANDARD 23)

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
    DEPENDS ${RCS_DIR}/embed.sh
    COMMENT "Embedding resources with embed.sh"
)

add_custom_target(EmbedResources ALL DEPENDS ${RCS_DIR}/.modules/timestamp)

# Collect framework sources
file(GLOB_RECURSE REV_SOURCES
    ${SRC_DIR}/**.cpp
    ${SRC_DIR}/**.ixx
    ${EXT_DIR}/**.ixx
    ${RCS_DIR}/.modules/**.ixx
)

# Exclude files based on OS (e.g. mac builds ignore files called example.mac.cpp, etc...)
if(WIN32)
  set(_exclude_regex "\\.(mac|osx|ios|lnx|linux|unix|android)\\.")
elseif(APPLE)
  set(_exclude_regex "\\.(win|lnx|linux|unix|android)\\.")
elseif(UNIX)
  set(_exclude_regex "\\.(win|mac|osx|ios)\\.")
endif()

# Exclude platform-specific sources
foreach(src ${REV_SOURCES})
  if(src MATCHES "${_exclude_regex}")
    list(REMOVE_ITEM REV_SOURCES ${src})
  endif()
endforeach()

add_library(Rev STATIC ${REV_SOURCES})
add_dependencies(Rev EmbedResources)

find_package(OpenGL REQUIRED)

target_include_directories(Rev PUBLIC
    ${SRC_DIR}
    ${RCS_DIR}/.modules
    ${EXT_DIR}
)

target_compile_definitions(Rev PUBLIC GLEW_STATIC)

target_link_libraries(Rev PUBLIC
    ${EXT_DIR}/glfw/bin/win/vc2022/glfw3.lib
    ${EXT_DIR}/glew/bin/win/x64/glew32s.lib
    ${EXT_DIR}/vulkan/bin/win/x64/vulkan-1.lib
    ${EXT_DIR}/freetype/bin/win/x64/freetype.lib
    OpenGL::GL
)
