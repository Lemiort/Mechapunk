#pragma once

// Helpers shared by the chronicle's history sections in the inspector:
// `InspectorHistory.cpp` (the robot page) and `InspectorChronicle.cpp` (the
// room, the building and the organization page). One entry must read alike
// wherever it is shown, so its words, its money and the size of its scrolling
// table live here instead of a copy in each file. Never include this header
// from outside app/.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <imgui.h>

#include "mechapunk/sim/Chronicle.hpp"
#include "mechapunk/sim/Social.hpp"
#include "mechapunk/sim/World.hpp"

namespace mechapunk {

// Long lists scroll inside a table this many rows high, and only the rows on
// screen are drawn (ImGuiListClipper): a robot or a busy building can have
// `kChronicleCap` entries.
inline constexpr int kVisibleRows = 12;

inline constexpr ImGuiTableFlags kListFlags =
    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
    ImGuiTableFlags_ScrollY;

// Height of a scrolling list of `rows` rows plus its header.
inline ImVec2 ListSize(std::size_t rows) {
    const auto shown =
        static_cast<float>(std::min<std::size_t>(rows, kVisibleRows)) + 1.5F;
    return {0.0F, shown * ImGui::GetFrameHeightWithSpacing()};
}

// Credits as "1 234 567 cr", the money format the Chronicle tab uses, so one
// entry reads alike wherever it is shown.
inline std::string Credits(std::int64_t credits) {
    // 1234567 -> "1 234 567 cr"
    std::string digits = std::to_string(credits < 0 ? -credits : credits);
    std::string grouped;
    for (std::size_t index = 0; index < digits.size(); ++index) {
        if (index > 0 && (digits.size() - index) % 3 == 0) {
            grouped += ' ';
        }
        grouped += digits[index];
    }
    return std::format("{}{} cr", credits < 0 ? "-" : "", grouped);
}

// What an entry records, in the few words a table column allows: the Chronicle
// tab's labels, so one story reads the same wherever it is shown. Only the
// kinds `World::Chronicle` keeps get a name of their own; a `Remembered` entry
// reads better by its memory kind.
inline std::string_view WhatName(const sim::ChronicleEntry& entry) {
    switch (entry.kind) {
        case sim::EventKind::Scrapped:        return "scrapped";
        case sim::EventKind::Arrived:         return "arrived in the city";
        case sim::EventKind::BoughtProperty:  return "bought a home";
        case sim::EventKind::UnitOpened:      return "a unit opened";
        case sim::EventKind::UnitClosed:      return "a unit closed";
        case sim::EventKind::Robbed:          return "robbery";
        case sim::EventKind::Arrested:        return "arrest";
        case sim::EventKind::Fought:          return "gang fight";
        case sim::EventKind::JoinedGang:      return "joined a gang";
        case sim::EventKind::LeftGang:        return "left a gang";
        case sim::EventKind::MoneySupplyChanged:
            return entry.credits > 0 ? "money created" : "money destroyed";
        case sim::EventKind::Remembered:
            switch (entry.memory) {
                case sim::MemoryKind::BecameCloseFriends: return "close friends";
                case sim::MemoryKind::BecameRivals:       return "rivals";
                case sim::MemoryKind::LostJob:            return "lost a job";
                case sim::MemoryKind::Promoted:           return "promoted";
                case sim::MemoryKind::Evicted:             return "evicted";
                default:                                   return "remembered";
            }
        default: return "story";
    }
}

}  // namespace mechapunk
