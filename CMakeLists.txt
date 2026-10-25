cmake_minimum_required(VERSION 3.24)

project(gamerpg VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GAMERPG_BUILD_TESTS "Build unit tests" ON)
option(GAMERPG_BUILD_CLI "Build the terminal runner" ON)
option(GAMERPG_BUILD_NARRATOR "Build the story-telling master (talks to a local language model)" ON)
option(GAMERPG_BUILD_TOOLS "Build offline tools (game master training)" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

add_library(gamerpg_warnings INTERFACE)
if(MSVC)
    target_compile_options(gamerpg_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(gamerpg_warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        # Designated initializers that skip members with defaults are intended.
        -Wno-missing-field-initializers)
endif()

add_subdirectory(core)

if(GAMERPG_BUILD_NARRATOR)
    add_subdirectory(narrator)
endif()

if(GAMERPG_BUILD_CLI)
    if(NOT GAMERPG_BUILD_NARRATOR)
        message(FATAL_ERROR "the terminal runner needs GAMERPG_BUILD_NARRATOR")
    endif()
    add_subdirectory(cli)
endif()

if(GAMERPG_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(GAMERPG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
