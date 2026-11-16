# Headless city simulation. Pure C++ with no engine or OpenGL dependency, so
# it runs in tools and tests without a window.
add_library(mechapunk_sim STATIC
    src/Careers.cpp
    src/City.cpp
    src/GameTime.cpp
    src/LifeCycle.cpp
    src/Migrations.cpp
    src/Random.cpp
    src/Save.cpp
    src/Social.cpp
    src/Venues.cpp
    src/World.cpp
)
add_library(mechapunk::sim ALIAS mechapunk_sim)
target_include_directories(mechapunk_sim PUBLIC include)
target_compile_features(mechapunk_sim PUBLIC cxx_std_23)
# Save.hpp exposes the glaze JSON tree that migrations edit.
target_link_libraries(mechapunk_sim PUBLIC glaze::glaze)
