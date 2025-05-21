cmake_minimum_required(VERSION 3.10)

# Project
project(HelloWorld)

# Use cpp 23
set(CMAKE_CXX_STANDARD 23)

# Define source and external directories
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(EXT_DIR ${CMAKE_SOURCE_DIR}/external)

# Get sources
file(GLOB_RECURSE SOURCES
    ${SRC_DIR}/**.c ${SRC_DIR}/**.cpp ${SRC_DIR}/**.ixx
    ${EXT_DIR}/**.c ${EXT_DIR}/**.cpp ${EXT_DIR}/**.ixx
)

# Set libraries to link
set(LINK
    ${EXT_DIR}/webgpu/bin/windows-x86_64/wgpu_native.dll.lib
    ${EXT_DIR}/glfw/bin/win/vc2022/glfw3.lib
)

# Set binaries to copy
set(COPY
    ${EXT_DIR}/webgpu/bin/windows-x86_64/wgpu_native.dll
)

# Create target, include, link
add_executable(${PROJECT_NAME} ${SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE ${EXT_DIR})
target_link_libraries(${PROJECT_NAME} ${LINK})

# Copy specified dlls
foreach(copy_file IN LISTS COPY)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${copy_file}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
endforeach()