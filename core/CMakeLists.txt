# Rules core: no graphics, no global state. Must never depend on bindings/ or godot/.
include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(gamerpg_core STATIC
    dice/rng.cpp
    dice/d20.cpp
    dice/dice_expr.cpp
    character/attributes.cpp
    character/ruleset.cpp
    character/character_sheet.cpp
    data/json_io.cpp
    data/class_registry.cpp
    data/text_table.cpp
)
add_library(gamerpg::core ALIAS gamerpg_core)

target_include_directories(gamerpg_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gamerpg_core
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE gamerpg_warnings
)
