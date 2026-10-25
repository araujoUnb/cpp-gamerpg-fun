# Story-telling master: talks to a local language model and checks what it asks for.
# Depends on the core; the core never depends on it.
include(FetchContent)

FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.23.1
    GIT_SHALLOW TRUE
    # Header only and plain HTTP: the header is used directly, without the library's
    # own CMake project (which would look for OpenSSL and compression libraries).
    SOURCE_SUBDIR no-cmake-project
)
FetchContent_MakeAvailable(httplib)

find_package(Threads REQUIRED)

add_library(gamerpg_narrator STATIC
    config.cpp
    situation.cpp
    narration.cpp
    narrator.cpp
    bank.cpp
    http_transport.cpp
)
add_library(gamerpg::narrator ALIAS gamerpg_narrator)

target_include_directories(gamerpg_narrator PUBLIC ${PROJECT_SOURCE_DIR})
target_include_directories(gamerpg_narrator SYSTEM PRIVATE ${httplib_SOURCE_DIR})
target_link_libraries(gamerpg_narrator
    PUBLIC gamerpg::core Threads::Threads
    PRIVATE gamerpg_warnings
)
if(WIN32)
    target_link_libraries(gamerpg_narrator PRIVATE ws2_32)
endif()
