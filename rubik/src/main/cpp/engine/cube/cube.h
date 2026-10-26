#pragma once

#include <array>
#include <cstdint>

namespace cube {

constexpr int NUM_CORNERS = 8;
constexpr int NUM_EDGES = 12;

struct CubeState {
    std::array<uint8_t, NUM_CORNERS> corner_positions;
    std::array<uint8_t, NUM_CORNERS> corner_orientations;
    std::array<uint8_t, NUM_EDGES> edge_positions;
    std::array<uint8_t, NUM_EDGES> edge_orientations;
};

CubeState solved_cube();

bool is_solved(const CubeState& c);

enum class Move : uint8_t {
    U, U_prime, U2,
    D, D_prime, D2,
    R, R_prime, R2,
    L, L_prime, L2,
    F, F_prime, F2,
    B, B_prime, B2,
};

constexpr int NUM_MOVES = 18;

void apply_move(CubeState& c, Move m);

}
