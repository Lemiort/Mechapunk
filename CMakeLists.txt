cmake_minimum_required(VERSION 3.21 FATAL_ERROR)
project(Mechapunk LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The engine is the Conan package nee/0.1.0 and brings GLFW, spdlog, GLM and
# Dear ImGui with it. The simulation writes its saves with glaze.
find_package(nee REQUIRED CONFIG)
find_package(glaze CONFIG REQUIRED)

include(CTest)

# Headless simulation library and its command-line runner.
add_subdirectory(sim)
add_executable(mechapunk-sim tools/sim_runner/main.cpp)
target_link_libraries(mechapunk-sim PRIVATE mechapunk::sim)

# Floor plans and prop models: geometry only, shared by the game and tests.
add_library(mechapunk_layout STATIC
    app/Interior.cpp
    app/InteriorHomes.cpp
    app/InteriorStores.cpp
    app/InteriorWorkplaces.cpp
    app/OrganizationLists.cpp
    app/Props.cpp)
target_include_directories(mechapunk_layout PUBLIC app)
target_link_libraries(mechapunk_layout PUBLIC nee::nee mechapunk::sim)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

add_executable(mechapunk
    app/CityView.cpp
    app/CityDrivers.cpp
    app/CityInteriors.cpp
    app/CityLights.cpp
    app/CityMetro.cpp
    app/CityRobots.cpp
    app/CityStatic.cpp
    app/GameRenderer.cpp
    app/Inspector.cpp
    app/InspectorOrganization.cpp
    app/Links.cpp
    app/Statistics.cpp
    app/Rain.cpp
    app/SimulationHost.cpp
    app/main.cpp
)
target_link_libraries(mechapunk PRIVATE mechapunk_layout nee::nee mechapunk::sim)

# While the engine is the editable checkout in external/NEE, rebuild it
# before the game, so engine edits reach the game in one build.
set(NEE_EDITABLE_BUILD "${CMAKE_CURRENT_SOURCE_DIR}/external/NEE/build/Release")
if(EXISTS "${NEE_EDITABLE_BUILD}/CMakeCache.txt")
    # Listing the libraries as byproducts makes the game relink when they
    # change.
    set(NEE_LIBRARIES)
    foreach(library nee nee_imgui_backends)
        list(APPEND NEE_LIBRARIES
            "${NEE_EDITABLE_BUILD}/${CMAKE_STATIC_LIBRARY_PREFIX}${library}${CMAKE_STATIC_LIBRARY_SUFFIX}")
    endforeach()
    add_custom_target(nee_engine
        COMMAND ${CMAKE_COMMAND} --build "${NEE_EDITABLE_BUILD}" --target nee
        BYPRODUCTS ${NEE_LIBRARIES}
        COMMENT "Building the NEE engine checkout"
        USES_TERMINAL)
    add_dependencies(mechapunk nee_engine)
endif()

# Engine assets come from the package's resource directory: the checkout's
# root when editable, `res` in an installed package.
set(NEE_ASSET_CANDIDATES)
foreach(config RELEASE RELWITHDEBINFO MINSIZEREL DEBUG)
    list(APPEND NEE_ASSET_CANDIDATES ${nee_RES_DIRS_${config}})
endforeach()
find_path(NEE_ASSET_DIR shaders/DSPointLight.fsh
    PATHS ${NEE_ASSET_CANDIDATES} NO_DEFAULT_PATH NO_CACHE)
if(NOT NEE_ASSET_DIR)
    message(FATAL_ERROR "NEE assets not found in the nee package")
endif()

# Runtime assets: engine assets first, then game assets (when present) so the
# game can override engine files with the same name.
# Editing an asset, such as an engine shader, re-runs CMake to copy it again.
foreach(dir shaders fonts models textures)
    file(GLOB_RECURSE asset_files CONFIGURE_DEPENDS
        ${NEE_ASSET_DIR}/${dir}/* ${CMAKE_CURRENT_SOURCE_DIR}/assets/${dir}/*)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${asset_files})
    file(COPY ${NEE_ASSET_DIR}/${dir} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets/${dir})
        file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets/${dir}
             DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
    endif()
endforeach()

# UI font from the imgui package (Roboto, Apache License 2.0).
find_file(MECHAPUNK_UI_FONT Roboto-Medium.ttf
    PATHS
        "${imgui_PACKAGE_FOLDER_RELEASE}/res/fonts"
        "${imgui_PACKAGE_FOLDER_RELWITHDEBINFO}/res/fonts"
        "${imgui_PACKAGE_FOLDER_DEBUG}/res/fonts"
    NO_DEFAULT_PATH NO_CACHE)
if(NOT MECHAPUNK_UI_FONT)
    message(FATAL_ERROR "Roboto-Medium.ttf not found in the imgui package")
endif()
file(COPY "${MECHAPUNK_UI_FONT}" DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/fonts)
