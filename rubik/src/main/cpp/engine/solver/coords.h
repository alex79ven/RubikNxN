#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "cube/cube.h"

namespace cube::solver {

// coord encodings for Kociemba search so search does array lookups instead of touching the full CubeState
// conventions:
//   - MSD = slot 0 (ori[0] gets the highest digit weight)
//   - the redundant final digit is dropped; parity/sum recovers it on decode

using Coord = uint16_t;

// 8 corner twists in {0,1,2}, sum mod 3 == 0. base-3 encoding of ori[0-6]
constexpr Coord CORNER_ORI_COORDS = 2187;
Coord encode_corner_ori(const CubeState& c);
std::array<uint8_t, NUM_CORNERS> decode_corner_ori(Coord coord);

// 12 edge flips in {0,1}, sum mod 2 == 0. base-2 encoding of ori[0-10]
constexpr Coord EDGE_ORI_COORDS = 2048;
Coord encode_edge_ori(const CubeState& c);
std::array<uint8_t, NUM_EDGES> decode_edge_ori(Coord coord);

constexpr Coord SLICE_COORDS = 495;
constexpr Coord SLICE_GOAL   = 494;
Coord encode_slice(const CubeState& c);
std::array<uint8_t, 4> decode_slice(Coord coord);

struct Phase1Coords {
    Coord corner_ori;
    Coord edge_ori;
    Coord slice;
};

Phase1Coords phase1_coords_of(const CubeState& c);

// phase 2 permutation coords. inside G1 only permutations remain; all three
// use Lehmer + factorial-base so the solved cube is coord 0 for each
//   corner_perm:  8 corners in slots 0-7              [0..40319 (8!)]
//   edge_perm_ud: 8 non-equator edges in slots 0-7    [0..40319 (8!)]
//   slice_perm:   4 equator edges in slots 8-11       [0..23    (4!)]

constexpr Coord CORNER_PERM_COORDS  = 40320;
constexpr Coord EDGE_PERM_UD_COORDS = 40320;
constexpr Coord SLICE_PERM_COORDS   = 24;

Coord encode_corner_perm(const CubeState& c);
std::array<uint8_t, NUM_CORNERS> decode_corner_perm(Coord coord);

Coord encode_edge_perm_ud(const CubeState& c);
std::array<uint8_t, 8> decode_edge_perm_ud(Coord coord);

Coord encode_slice_perm(const CubeState& c);
std::array<uint8_t, 4> decode_slice_perm(Coord coord);

struct Phase2Coords {
    Coord corner_perm;
    Coord edge_perm_ud;
    Coord slice_perm;
};

Phase2Coords phase2_coords_of(const CubeState& c);

}
