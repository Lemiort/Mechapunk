#pragma once

// What City.cpp and CityBuildings.cpp share while generating a city: the
// kinds of workplace, the placement helpers and the two building makers.

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "mechapunk/sim/City.hpp"

namespace mechapunk::sim::city_build {

struct WorkplaceType {
    std::string_view name;
    std::string_view workspace;
    Shift shift;
};

// Shift times in minutes of the day; wages in credits per shift.
inline constexpr std::array kWorkplaceTypes{
    WorkplaceType{.name = "Foundry",
                  .workspace = "Smelting Bay",
                  .shift = {.startMinute = 6 * 60, .lengthMinutes = 600,
                            .wage = 130}},
    WorkplaceType{.name = "Assembly Line",
                  .workspace = "Assembly Cell",
                  .shift = {.startMinute = 7 * 60, .lengthMinutes = 480,
                            .wage = 100}},
    WorkplaceType{.name = "Repair Shop",
                  .workspace = "Service Bay",
                  .shift = {.startMinute = 8 * 60, .lengthMinutes = 600,
                            .wage = 110}},
    WorkplaceType{.name = "Corp Tower",
                  .workspace = "Office Floor",
                  .shift = {.startMinute = 9 * 60, .lengthMinutes = 540,
                            .wage = 150}},
    WorkplaceType{.name = "Noodle Bar",
                  .workspace = "Kitchen",
                  .shift = {.startMinute = 14 * 60, .lengthMinutes = 480,
                            .wage = 90}},
    WorkplaceType{.name = "Data Farm",
                  .workspace = "Server Hall",
                  .shift = {.startMinute = 22 * 60, .lengthMinutes = 480,
                            .wage = 120}},
};

// An intersection in the given columns with a free corner, if one can be
// found in a few tries; its corner is then taken.
NodeId RandomNodeInColumns(const CityParams& params, Pcg32& random,
                           std::uint32_t firstColumn, std::uint32_t lastColumn,
                           std::vector<std::uint32_t>& buildingsAt);

// How close an intersection is to the middle of the grid: 1 at the middle,
// 0 at the corners.
double Centrality(NodeId node, std::uint32_t gridSize);

// How much a city of `residents` rises in the middle: 0 for a town, 1 for a
// big city.
double Urbanity(std::uint32_t residents);

// A whole number between `edge` and `central` by centrality.
std::uint32_t Lerp(std::uint32_t edge, std::uint32_t central, double centrality);

Building MakeHabitat(const CityParams& params, Pcg32& random,
                     std::vector<std::uint32_t>& buildingsAt,
                     EntityIdAllocator& ids, std::uint32_t number,
                     std::uint32_t& storeRotation, bool dormitory);

Building MakeWorkplace(const CityParams& params, Pcg32& random,
                       std::vector<std::uint32_t>& buildingsAt,
                       EntityIdAllocator& ids,
                       std::array<std::uint32_t, kWorkplaceTypes.size()>& counts);

}  // namespace mechapunk::sim::city_build
