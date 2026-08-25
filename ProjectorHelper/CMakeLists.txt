cmake_minimum_required(VERSION 3.21)
project(LithoRevProjector LANGUAGES CXX)

# Deliberately a separate, non-modules C++ project from Demo/ -- this is the
# projector-window replacement built to route around a freeze bug that
# turned out to be triggered by fork()ing LithoRev's own process (which has
# an active OpenGL context) to make the projector window, not by anything
# about the window/rendering itself. Being a genuinely separate executable,
# spawned via posix_spawn() rather than fork()+exec() from within LithoRev,
# means LithoRev's own process never calls fork() at all, sidestepping that
# whole hazard. raylib (not Rev) renders it, per the decision to stop
# relying on Rev's own Linux X11/GL window code for this feature.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
)
FetchContent_MakeAvailable(raylib)

add_executable(LithoRevProjector src/main.cpp)
target_link_libraries(LithoRevProjector PRIVATE raylib)

# Built as a sibling of LithoRev (same Demo/ output dir) so it can be found
# at runtime via a relative path next to LithoRev's own executable.
set_target_properties(LithoRevProjector PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Demo
)
