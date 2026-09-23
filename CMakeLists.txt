cmake_minimum_required(VERSION 3.22)

# One entry point for everything:
#   host unit tests:   cmake --preset host-tests
#   firmware:          cmake --preset h7s3l8-node1   (cross-compiles, see CMakePresets.json)
project(robot_joint_stack VERSION 0.1.0 LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(RJS_BOARD "" CACHE STRING "Board port to build (empty = host tests only). Available: nucleo_h7s3l8")

# Strict warnings for our own code (not for vendor code).
set(RJS_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
                 -Wstrict-prototypes -Wmissing-prototypes -Werror)

add_subdirectory(common)

if(RJS_BOARD STREQUAL "")
    enable_testing()
    add_subdirectory(tests)
else()
    add_subdirectory(boards/${RJS_BOARD})
endif()
