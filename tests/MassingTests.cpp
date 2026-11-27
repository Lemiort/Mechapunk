#include <gtest/gtest.h>

#include <cstdint>

#include "Interior.hpp"
#include "Massing.hpp"

namespace sim = mechapunk::sim;
using mechapunk::MassingOf;
using mechapunk::RoofOf;
using mechapunk::SignHeightMeters;

namespace {

constexpr sim::BuildingStyle kStyles[] = {
    sim::BuildingStyle::Plain,      sim::BuildingStyle::Setback, sim::BuildingStyle::Stepped,
    sim::BuildingStyle::TwinTowers, sim::BuildingStyle::Plinth,  sim::BuildingStyle::Gabled,
    sim::BuildingStyle::Sawtooth,   sim::BuildingStyle::Tower};

}  // namespace

TEST(Massing, PlainIsTheWholeFootprint) {
    for (std::uint32_t floors = 1; floors <= 12; ++floors) {
        const auto boxes = MassingOf(sim::BuildingStyle::Plain, floors);
        ASSERT_EQ(boxes.size(), 1U);
        EXPECT_FLOAT_EQ(boxes[0].x1 - boxes[0].x0, mechapunk::kFootprintWidthMeters);
        EXPECT_FLOAT_EQ(boxes[0].z1 - boxes[0].z0, mechapunk::kFootprintDepthMeters);
        EXPECT_EQ(boxes[0].firstFloor, 0U);
        EXPECT_EQ(boxes[0].endFloor, floors);
        EXPECT_TRUE(RoofOf(sim::BuildingStyle::Plain, floors).empty());
    }
}

TEST(Massing, EveryStyleStaysInsideTheFootprintAndStandsOnTheGround) {
    for (const sim::BuildingStyle style : kStyles) {
        for (std::uint32_t floors = 1; floors <= 14; ++floors) {
            const auto boxes = MassingOf(style, floors);
            ASSERT_FALSE(boxes.empty());
            // The base reaches the street across the whole footprint.
            EXPECT_EQ(boxes.front().firstFloor, 0U);
            EXPECT_FLOAT_EQ(boxes.front().x1 - boxes.front().x0,
                            mechapunk::kFootprintWidthMeters);
            std::uint32_t highest = 0;
            for (const auto& box : boxes) {
                EXPECT_GE(box.x0, -mechapunk::kFootprintWidthMeters * 0.5F);
                EXPECT_LE(box.x1, mechapunk::kFootprintWidthMeters * 0.5F);
                EXPECT_GE(box.z0, -mechapunk::kFootprintDepthMeters * 0.5F);
                EXPECT_LE(box.z1, mechapunk::kFootprintDepthMeters * 0.5F);
                EXPECT_LT(box.x0, box.x1);
                EXPECT_LT(box.firstFloor, box.endFloor);
                highest = std::max(highest, box.endFloor);
            }
            // Only the tower's crown rises above the building's floors.
            EXPECT_EQ(highest, style == sim::BuildingStyle::Tower ? floors + 1 : floors);
            for (const auto& piece : RoofOf(style, floors)) {
                EXPECT_GE(piece.x - (piece.sizeX * 0.5F), -mechapunk::kFootprintWidthMeters * 0.55F);
                EXPECT_LE(piece.x + (piece.sizeX * 0.5F), mechapunk::kFootprintWidthMeters * 0.55F);
            }
            EXPECT_GT(SignHeightMeters(style, floors), 0.0F);
            EXPECT_LE(SignHeightMeters(style, floors),
                      static_cast<float>(boxes.front().endFloor) * mechapunk::kFloorHeightMeters);
        }
    }
}

TEST(Massing, StylesLookDifferent) {
    EXPECT_EQ(MassingOf(sim::BuildingStyle::Setback, 9).size(), 2U);
    EXPECT_EQ(MassingOf(sim::BuildingStyle::Stepped, 9).size(), 3U);
    EXPECT_EQ(MassingOf(sim::BuildingStyle::TwinTowers, 9).size(), 3U);
    EXPECT_EQ(MassingOf(sim::BuildingStyle::Plinth, 9).size(), 2U);
    EXPECT_EQ(MassingOf(sim::BuildingStyle::Tower, 9).size(), 2U);
    EXPECT_FALSE(RoofOf(sim::BuildingStyle::Gabled, 4).empty());
    EXPECT_FALSE(RoofOf(sim::BuildingStyle::Sawtooth, 2).empty());
    // A style the floor count cannot carry is a plain box, not a crash.
    EXPECT_EQ(MassingOf(sim::BuildingStyle::Plinth, 2).size(), 1U);
}
