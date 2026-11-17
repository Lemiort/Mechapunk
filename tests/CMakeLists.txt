find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(sim_tests
    CareersTests.cpp
    CityTests.cpp
    EconomyTests.cpp
    LifeCycleTests.cpp
    MetroTests.cpp
    RandomTests.cpp
    SaveTests.cpp
    SocialTests.cpp
    TransitTests.cpp
    WeatherTests.cpp
    WorldTests.cpp
)
target_link_libraries(sim_tests PRIVATE mechapunk::sim GTest::gtest_main)
target_compile_definitions(sim_tests PRIVATE
    MECHAPUNK_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
gtest_discover_tests(sim_tests)

# The game's floor plans, without a window or renderer.
add_executable(layout_tests LayoutTests.cpp)
target_link_libraries(layout_tests PRIVATE mechapunk_layout GTest::gtest_main)
gtest_discover_tests(layout_tests)
