cmake_minimum_required(VERSION 3.21 FATAL_ERROR)
project(Mechapunk LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies are resolved by this repository's conanfile.txt; NEE's own
# find_package calls reuse them.
find_package(glfw3 REQUIRED)
find_package(spdlog REQUIRED)
find_package(glaze CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

include(CTest)

# The engine, consumed as a git submodule. Only the `nee` library is built.
add_subdirectory(external/NEE EXCLUDE_FROM_ALL)

# Headless simulation library and its command-line runner.
add_subdirectory(sim)
add_executable(mechapunk-sim tools/sim_runner/main.cpp)
target_link_libraries(mechapunk-sim PRIVATE mechapunk::sim)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

add_executable(mechapunk
    app/CityView.cpp
    app/GameRenderer.cpp
    app/Inspector.cpp
    app/SimulationHost.cpp
    app/main.cpp
)
target_link_libraries(mechapunk PRIVATE
    nee::nee mechapunk::sim glfw spdlog::spdlog
)

# Runtime assets: engine assets first, then game assets (when present) so the
# game can override engine files with the same name.
foreach(dir shaders fonts models textures)
    file(COPY ${NEE_ASSET_DIR}/${dir} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets/${dir})
        file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets/${dir}
             DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
    endif()
endforeach()

# UI font from the imgui package (Roboto, Apache License 2.0).
file(COPY "${NEE_IMGUI_BACKEND_DIR}/../fonts/Roboto-Medium.ttf"
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/fonts)
