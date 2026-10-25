# Terminal runner: plays one scenario with text output. Development tool.
add_executable(gamerpg_cli main.cpp)
target_link_libraries(gamerpg_cli PRIVATE gamerpg::core gamerpg_warnings)
target_compile_definitions(gamerpg_cli PRIVATE GAMERPG_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
