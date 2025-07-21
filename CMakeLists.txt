cmake_minimum_required(VERSION 3.10)

# Project
project(HelloWorld)

# Use cpp 23
set(CMAKE_CXX_STANDARD 23)

# Define source and external directories
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(EXT_DIR ${CMAKE_SOURCE_DIR}/external)
set(RCS_DIR ${CMAKE_SOURCE_DIR}/resources)

# Custom command to run the resource embedding script
add_custom_command(
    OUTPUT ${RCS_DIR}/.modules/timestamp
    COMMAND ${CMAKE_COMMAND} -E echo "Running resource embed script..."
    COMMAND ${CMAKE_COMMAND} -E env bash ${CMAKE_SOURCE_DIR}/scripts/Create_Resource_Modules.sh
    COMMAND ${CMAKE_COMMAND} -E touch ${RCS_DIR}/.modules/timestamp
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS ${RCS_DIR}/embed.sh
    COMMENT "Embedding resources with embed.sh"
)

add_custom_target(EmbedResources ALL DEPENDS ${RCS_DIR}/.modules/timestamp)

# Get sources
file(GLOB_RECURSE SOURCES
    ${SRC_DIR}/**.c ${SRC_DIR}/**.cpp ${SRC_DIR}/**.ixx
    ${EXT_DIR}/**.c ${EXT_DIR}/**.cpp ${EXT_DIR}/**.ixx
    ${RCS_DIR}/.modules/**.ixx
)

# Set libraries to link
set(LINK
    ${EXT_DIR}/glfw/bin/win/vc2022/glfw3.lib
    ${EXT_DIR}/glew/bin/win/x64/glew32s.lib
    ${EXT_DIR}/vulkan/bin/win/x64/vulkan-1.lib
)

# Set binaries to copy
set(COPY
   
)

# Create target, include, link
add_executable(${PROJECT_NAME} ${SOURCES})
add_dependencies(${PROJECT_NAME} EmbedResources)

# Add debug definition
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    GLEW_STATIC
)

find_package(OpenGL REQUIRED)

# Add include, linked libraries
target_include_directories(${PROJECT_NAME} PRIVATE ${EXT_DIR} ${EXT_DIR}/skia)
target_link_libraries(${PROJECT_NAME} ${LINK} OpenGL::GL)

# Copy specified dlls
foreach(copy_file IN LISTS COPY)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${copy_file}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
endforeach()