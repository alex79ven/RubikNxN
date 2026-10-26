#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "cube/cube.h"

namespace cube {

std::string_view to_string(Move m);

std::optional<Move> parse_move(std::string_view s);

std::optional<std::vector<Move>> parse_sequence(std::string_view s);

}
