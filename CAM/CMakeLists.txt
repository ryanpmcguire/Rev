cmake_minimum_required(VERSION 3.10)
project(CAMDemo)

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

# Add executable
add_executable(CAMDemo)

# Set target properties
set_target_properties(CAMDemo PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  UNITY_BUILD OFF
  CXX_SCAN_FOR_MODULES ON
)

# Add implementation sources
target_sources(CAMDemo
    PUBLIC ${CPP_SOURCES} ${OBJCPP_SOURCES}
)

# Add module interface sources
target_sources(CAMDemo
    PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES ${MODULE_SOURCES}
)

find_package(OpenCASCADE REQUIRED)

target_include_directories(CAMDemo PRIVATE
    ${OpenCASCADE_INCLUDE_DIR}
)

target_link_libraries(CAMDemo PRIVATE Rev Geometry ${OpenCASCADE_LIBRARIES})

if(WIN32)
  set(CAM_APP_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest")
  target_link_options(CAMDemo PRIVATE
    "LINKER:/manifest:embed"
    "LINKER:/manifestinput:${CAM_APP_MANIFEST}"
  )
endif()

target_compile_definitions(CAMDemo PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
)

target_compile_definitions(Rev PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
)