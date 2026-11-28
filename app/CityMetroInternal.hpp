#pragma once

// Sizes and colors the drawing of the elevated metro shares between its lines
// and stations (CityMetro.cpp) and its depots (CityDepots.cpp). Private to
// those files.

#include "CityViewInternal.hpp"

namespace mechapunk {
namespace cityview_detail {

// The elevated metro. Row lines run with their deck top 8 m over the street
// and column lines 14 m, so they cross without meeting. Each line has two
// tracks 1.8 m either side of its middle; a station has a side platform
// beside each track, with stairs down to the pavement past its end.
// Lengths in meters.
inline constexpr float kRowDeck = 8.0F;

inline constexpr float kColumnDeck = 14.0F;

inline constexpr float kDeckThickness = 0.7F;

inline constexpr float kTrackOffset = 1.8F;

inline constexpr float kDeckHalf = 3.6F;

// Each rail from its track's middle.
inline constexpr float kRailGauge = 0.7F;

inline constexpr float kPlatformInner = 3.4F;

inline constexpr float kPlatformOuter = 6.4F;

inline constexpr float kPlatformHalfLength = 14.0F;

// Units.
inline constexpr float kStairsAcross = (kStreetWidth * 0.5F) + (0.8F * kS);

inline constexpr vec3 kTrackSteel{0.22F, 0.23F, 0.27F};

inline constexpr vec3 kRail{0.55F, 0.56F, 0.6F};

inline constexpr vec3 kCanopy{0.3F, 0.34F, 0.4F};

inline constexpr vec3 kStationSign{0.1F, 0.85F, 1.0F};

inline constexpr vec3 kBumper{1.0F, 0.15F, 0.1F};

inline float DeckHeight(sim::LineAxis axis) {
    return (axis == sim::LineAxis::Row ? kRowDeck : kColumnDeck) * kS;
}

inline vec3 AlongOf(sim::LineAxis axis) {
    return axis == sim::LineAxis::Row ? vec3{1.0F, 0.0F, 0.0F} : vec3{0.0F, 0.0F, 1.0F};
}

}  // namespace cityview_detail
}  // namespace mechapunk
