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

include(CTest)

# The engine, consumed as a git submodule. Only the `nee` library is built.
add_subdirectory(external/NEE EXCLUDE_FROM_ALL)

add_executable(mechapunk app/main.cpp)
target_link_libraries(mechapunk PRIVATE nee::nee glfw spdlog::spdlog)

# Runtime assets: engine assets first, then game assets (when present) so the
# game can override engine files with the same name.
foreach(dir shaders fonts models textures)
    file(COPY ${NEE_ASSET_DIR}/${dir} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets/${dir})
        file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets/${dir}
             DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
    endif()
endforeach()
