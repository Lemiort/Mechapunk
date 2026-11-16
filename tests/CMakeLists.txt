find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(sim_tests
    CareersTests.cpp
    CityTests.cpp
    RandomTests.cpp
    SaveTests.cpp
    SocialTests.cpp
    WorldTests.cpp
)
target_link_libraries(sim_tests PRIVATE mechapunk::sim GTest::gtest_main)
target_compile_definitions(sim_tests PRIVATE
    MECHAPUNK_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
gtest_discover_tests(sim_tests)
