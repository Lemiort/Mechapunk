#pragma once

#include <string>

#include "mechapunk/sim/Random.hpp"

namespace mechapunk::sim {

// "KX-417 Vera": a model prefix, a serial number and a given name, drawn in
// that order.
[[nodiscard]] std::string RandomRobotName(Pcg32& random);

}  // namespace mechapunk::sim
