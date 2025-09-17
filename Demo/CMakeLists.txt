cmake_minimum_required(VERSION 3.10)
project(RevDemo)

set(CMAKE_CXX_STANDARD 23)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/source)

file(GLOB_RECURSE DEMO_SOURCES
    ${SRC_DIR}/**.cpp
    ${SRC_DIR}/**.ixx
)

add_executable(HelloWorld ${DEMO_SOURCES})
target_link_libraries(HelloWorld PRIVATE Rev)

target_compile_definitions(HelloWorld PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
)

target_compile_definitions(Rev PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
)