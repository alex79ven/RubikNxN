#pragma once

#include <array>
#include <cstdint>

#include "cube/cube.h"
#include "solver/coords.h"

namespace cube::solver {

// phase-2 symmetry subgroup — the rotations/reflections of the whole cube that
// preserve the G1 move set (u/d/r2/l2/f2/b2). generators used:
//   S_U2  — 180 around vertical axis (order 2)
//   S_F2  — 180 around F-B axis (order 2)
//   S_LR2 — reflection through the L/R midplane (order 2)
constexpr int NUM_SYM = 8;

// conj_move[s][m] = the move index equivalent to (s^-1 * m * s) as a face turn.
const std::array<std::array<uint8_t, NUM_MOVES>, NUM_SYM>& sym_conj_move();

// apply the s-th symmetry as a state transform, returning s * c * s^-1
CubeState apply_symmetry(int s, const CubeState& c);

using SymCornerPermTable  = std::array<std::array<Coord, CORNER_PERM_COORDS>, NUM_SYM>;
using SymEdgePermUDTable  = std::array<std::array<Coord, EDGE_PERM_UD_COORDS>, NUM_SYM>;
using SymSlicePermTable   = std::array<std::array<Coord, SLICE_PERM_COORDS>,   NUM_SYM>;

const SymCornerPermTable& sym_corner_perm();
const SymEdgePermUDTable& sym_edge_perm_ud();
const SymSlicePermTable&  sym_slice_perm();

}
