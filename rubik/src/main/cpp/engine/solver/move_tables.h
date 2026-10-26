#pragma once

#include <array>
#include <cstdint>

#include "cube/cube.h"
#include "solver/coords.h"

namespace cube::solver {

// precomputed move tables, turning each move during IDA* into a table lookup
// layout keeps next coords contiguous so IDA*'s successor expansion is one cache line
// phase 2 tables are indexed by dense G1 index (0-9), not the Move enum

constexpr int NUM_G1_MOVES = 10;

constexpr std::array<Move, NUM_G1_MOVES> G1_MOVES = {
    Move::U, Move::U_prime, Move::U2,
    Move::D, Move::D_prime, Move::D2,
    Move::R2, Move::L2, Move::F2, Move::B2,
};

// -1 if `m` isn't a G1 move
constexpr int g1_index(Move m) {
    for (int i = 0; i < NUM_G1_MOVES; ++i) {
        if (G1_MOVES[i] == m) return i;
    }
    return -1;
}

using CornerOriTable = std::array<std::array<Coord, NUM_MOVES>, CORNER_ORI_COORDS>;
using EdgeOriTable   = std::array<std::array<Coord, NUM_MOVES>, EDGE_ORI_COORDS>;
using SliceTable     = std::array<std::array<Coord, NUM_MOVES>, SLICE_COORDS>;

const CornerOriTable& corner_ori_move_table();
const EdgeOriTable&   edge_ori_move_table();
const SliceTable&     slice_move_table();

using CornerPermTable = std::array<std::array<Coord, NUM_G1_MOVES>, CORNER_PERM_COORDS>;
using EdgePermUDTable = std::array<std::array<Coord, NUM_G1_MOVES>, EDGE_PERM_UD_COORDS>;
using SlicePermTable  = std::array<std::array<Coord, NUM_G1_MOVES>, SLICE_PERM_COORDS>;

const CornerPermTable& corner_perm_move_table();
const EdgePermUDTable& edge_perm_ud_move_table();
const SlicePermTable&  slice_perm_move_table();

}
