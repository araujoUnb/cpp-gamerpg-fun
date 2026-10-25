include(FetchContent)

FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(doctest)

add_executable(gamerpg_tests
    main.cpp
    dice/test_rng.cpp
    dice/test_d20.cpp
    dice/test_dice_expr.cpp
)
target_link_libraries(gamerpg_tests PRIVATE gamerpg::core doctest::doctest gamerpg_warnings)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(gamerpg_tests)
