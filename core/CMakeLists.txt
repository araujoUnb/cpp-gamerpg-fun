# Rules core: no graphics, no global state. Must never depend on app/.
add_library(gamerpg_core STATIC
    dice/rng.cpp
    dice/d20.cpp
    dice/dice_expr.cpp
)
add_library(gamerpg::core ALIAS gamerpg_core)

target_include_directories(gamerpg_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gamerpg_core PRIVATE gamerpg_warnings)
