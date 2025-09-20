cmake_minimum_required(VERSION 3.10)

# --- macOS SDK / sysroot setup ----------------------------------------------

if(APPLE)
  # 1) Allow user to override via -DCMAKE_OSX_SYSROOT=... at configure time.
  #    If not provided, auto-detect with xcrun.
  if(NOT CMAKE_OSX_SYSROOT)
    execute_process(
      COMMAND xcrun --sdk macosx --show-sdk-path
      OUTPUT_VARIABLE MACOSX_SDK_PATH
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(EXISTS "${MACOSX_SDK_PATH}")
      set(CMAKE_OSX_SYSROOT "${MACOSX_SDK_PATH}" CACHE PATH "macOS SDK" FORCE)
    else()
      message(WARNING "Could not detect macOS SDK path via xcrun.")
    endIf()
  endif()

  # 2) Set a consistent deployment target (change if you need a newer min).
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Min macOS version" FORCE)

  # 3) Ensure compile and link steps *see* the SDK, but only if we have one.
  if(CMAKE_OSX_SYSROOT)
    # Compilers pick up headers & libs from the SDK
    add_compile_options(-isysroot "${CMAKE_OSX_SYSROOT}")

    # Linker uses the same SDK root. DO NOT add an empty -syslibroot,
    # so guard it behind the if() as shown.
    add_link_options(-Wl,-syslibroot,"${CMAKE_OSX_SYSROOT}")

    # Optional: help tools that consult SDKROOT env var
    set(ENV{SDKROOT} "${CMAKE_OSX_SYSROOT}")
  endif()
endif()

# Remaining
#--------------------------------------------------

project(RevRoot)

add_subdirectory(Rev)
add_subdirectory(demo)
