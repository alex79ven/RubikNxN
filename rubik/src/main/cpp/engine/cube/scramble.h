#pragma once

#include <cstdint>
#include <vector>

#include "cube/cube.h"

namespace cube {

constexpr int DEFAULT_SCRAMBLE_LENGTH = 25;

// deterministic given (length, seed). no two consecutive moves share a face
std::vector<Move> random_scramble(int length, uint64_t seed);

}
