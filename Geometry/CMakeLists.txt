cmake_minimum_required(VERSION 3.10)
project(RevGeometry)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API 1)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/source)

file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS
    ${SRC_DIR}/*.ixx
)

# The shared 2D geometry kernel: stoicheia, chains, profiles, and the
# crossing-number offsetting method. Consumed by both Sketch and CAM.
add_library(Geometry STATIC)

set_target_properties(Geometry PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  UNITY_BUILD OFF
  CXX_SCAN_FOR_MODULES ON
)

target_sources(Geometry
    PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES ${MODULE_SOURCES}
)

# Rev provides Rev.Core.Pos and the public include dirs (nlohmann, dbg).
target_link_libraries(Geometry PUBLIC Rev)

target_compile_definitions(Geometry PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
)
