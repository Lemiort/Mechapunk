#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <glm/ext/vector_float4.hpp>

#include "CityView.hpp"
#include "SimulationHost.hpp"

namespace mechapunk {

struct InspectorLine {
    std::string text;
    float size{20.0F};
    glm::vec4 color{0.85F, 0.88F, 0.95F, 1.0F};
};

// Describes the selected robot, room or open building for the side panel.
// Lines longer than `maxChars` are wrapped.
[[nodiscard]] std::vector<InspectorLine> DescribeSelection(
    const Selection& selection, const WorldFrame& frame, const CityView& city,
    const SimulationHost& host, std::size_t maxChars);

}  // namespace mechapunk
