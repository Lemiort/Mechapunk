find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(sim_tests
    GangRosterTests.cpp
    GangCrimeTests.cpp
    GangOfficerTests.cpp
    CareersTests.cpp
    ChronicleTests.cpp
    ConstructionTests.cpp
    SiteOfficeTests.cpp
    SiteSuppliesTests.cpp
    ConstructionCountersTests.cpp
    CityTests.cpp
    CityStreetTests.cpp
    CityStyleTests.cpp
    EconomyTests.cpp
    HousingTests.cpp
    VenueTests.cpp
    LifeCycleTests.cpp
    MetroTests.cpp
    RandomTests.cpp
    SaveTests.cpp
    SaveFixtureTests.cpp
    SaveFixtureTestsEarly.cpp
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
add_executable(
    layout_tests
    ChronicleListsTests.cpp
    GangListsTests.cpp
    LayoutTests.cpp
    MassingTests.cpp
    OrganizationListsTests.cpp
    ProjectListsTests.cpp
)
target_link_libraries(layout_tests PRIVATE mechapunk_layout GTest::gtest_main)
gtest_discover_tests(layout_tests)

# Every module must fit a small model's context: see docs/MODULES.md.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME ContextBudget
             COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/context_budget.py)
endif()
