# Offline tools: training and balancing. Not part of the game.
find_package(Threads REQUIRED)
add_executable(gamerpg_gm_train gm_train.cpp)
target_link_libraries(gamerpg_gm_train PRIVATE gamerpg::core gamerpg_warnings Threads::Threads)
target_compile_definitions(gamerpg_gm_train PRIVATE GAMERPG_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
