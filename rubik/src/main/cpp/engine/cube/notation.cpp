#include "cube/notation.h"

#include <array>
#include <cctype>

namespace cube {

namespace {

constexpr std::array<std::string_view, NUM_MOVES> MOVE_NAMES = {
    "U", "U'", "U2",
    "D", "D'", "D2",
    "R", "R'", "R2",
    "L", "L'", "L2",
    "F", "F'", "F2",
    "B", "B'", "B2",
};

std::optional<int> face_index(char c) {
    switch (c) {
        case 'U': return 0;
        case 'D': return 1;
        case 'R': return 2;
        case 'L': return 3;
        case 'F': return 4;
        case 'B': return 5;
        default:  return std::nullopt;
    }
}

}  // namespace

std::string_view to_string(Move m) {
    return MOVE_NAMES[static_cast<uint8_t>(m)];
}

std::optional<Move> parse_move(std::string_view s) {
    if (s.empty() || s.size() > 2) return std::nullopt;

    auto face = face_index(s[0]);
    if (!face) return std::nullopt;

    int variant = 0;  // 0=quarter, 1=prime, 2=half
    if (s.size() == 2) {
        if      (s[1] == '\'') variant = 1;
        else if (s[1] == '2')  variant = 2;
        else                   return std::nullopt;
    }

    return static_cast<Move>(*face * 3 + variant);
}

std::optional<std::vector<Move>> parse_sequence(std::string_view s) {
    std::vector<Move> result;

    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i >= s.size()) break;

        size_t start = i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;

        auto m = parse_move(s.substr(start, i - start));
        if (!m) return std::nullopt;
        result.push_back(*m);
    }

    return result;
}

}
