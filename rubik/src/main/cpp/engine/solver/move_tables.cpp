#include "solver/move_tables.h"

#include <memory>

namespace cube::solver {

namespace {

// seeds a CubeState from a decoded coord, leaving everything
// the coord doesn't cover at solved defaults (because each coord's
// transform under a move only reads the array it encodes)

CubeState state_with_corner_ori(const std::array<uint8_t, NUM_CORNERS>& ori) {
    CubeState c = solved_cube();
    c.corner_orientations = ori;
    return c;
}

CubeState state_with_edge_ori(const std::array<uint8_t, NUM_EDGES>& ori) {
    CubeState c = solved_cube();
    c.edge_orientations = ori;
    return c;
}

// puts equator edges at the given slots, non-equator edges fill the rest
CubeState state_with_slice(const std::array<uint8_t, 4>& slice_slots) {
    CubeState c = solved_cube();
    for (uint8_t k = 0; k < 4; ++k) {
        c.edge_positions[slice_slots[k]] = static_cast<uint8_t>(8 + k);
    }
    uint8_t next_non_equator = 0;
    for (uint8_t i = 0; i < NUM_EDGES; ++i) {
        bool is_slice = (i == slice_slots[0] || i == slice_slots[1] ||
                         i == slice_slots[2] || i == slice_slots[3]);
        if (!is_slice) c.edge_positions[i] = next_non_equator++;
    }
    return c;
}

CubeState state_with_corner_perm(const std::array<uint8_t, NUM_CORNERS>& perm) {
    CubeState c = solved_cube();
    c.corner_positions = perm;
    return c;
}

// only fills slots 0-7; equator slots stay at solved defaults
CubeState state_with_edge_perm_ud(const std::array<uint8_t, 8>& perm) {
    CubeState c = solved_cube();
    for (size_t i = 0; i < 8; ++i) c.edge_positions[i] = perm[i];
    return c;
}

// perm values are {0-3}; add 8 to recover the equator edge ids {8-11}
CubeState state_with_slice_perm(const std::array<uint8_t, 4>& perm) {
    CubeState c = solved_cube();
    for (size_t i = 0; i < 4; ++i) {
        c.edge_positions[8 + i] = static_cast<uint8_t>(perm[i] + 8);
    }
    return c;
}

// builders return unique_ptrs because the corner_perm / edge_perm_ud tables
// are ~1.4 MB each: too big for the getter's stack frame. getter caches the
// pointer as a function-local static

std::unique_ptr<CornerOriTable> build_corner_ori_table() {
    auto t = std::make_unique<CornerOriTable>();
    for (Coord c = 0; c < CORNER_ORI_COORDS; ++c) {
        CubeState s = state_with_corner_ori(decode_corner_ori(c));
        for (uint8_t m = 0; m < NUM_MOVES; ++m) {
            CubeState next = s;
            apply_move(next, static_cast<Move>(m));
            (*t)[c][m] = encode_corner_ori(next);
        }
    }
    return t;
}

std::unique_ptr<EdgeOriTable> build_edge_ori_table() {
    auto t = std::make_unique<EdgeOriTable>();
    for (Coord c = 0; c < EDGE_ORI_COORDS; ++c) {
        CubeState s = state_with_edge_ori(decode_edge_ori(c));
        for (uint8_t m = 0; m < NUM_MOVES; ++m) {
            CubeState next = s;
            apply_move(next, static_cast<Move>(m));
            (*t)[c][m] = encode_edge_ori(next);
        }
    }
    return t;
}

std::unique_ptr<SliceTable> build_slice_table() {
    auto t = std::make_unique<SliceTable>();
    for (Coord c = 0; c < SLICE_COORDS; ++c) {
        CubeState s = state_with_slice(decode_slice(c));
        for (uint8_t m = 0; m < NUM_MOVES; ++m) {
            CubeState next = s;
            apply_move(next, static_cast<Move>(m));
            (*t)[c][m] = encode_slice(next);
        }
    }
    return t;
}

std::unique_ptr<CornerPermTable> build_corner_perm_table() {
    auto t = std::make_unique<CornerPermTable>();
    for (Coord c = 0; c < CORNER_PERM_COORDS; ++c) {
        CubeState s = state_with_corner_perm(decode_corner_perm(c));
        for (int i = 0; i < NUM_G1_MOVES; ++i) {
            CubeState next = s;
            apply_move(next, G1_MOVES[i]);
            (*t)[c][i] = encode_corner_perm(next);
        }
    }
    return t;
}

std::unique_ptr<EdgePermUDTable> build_edge_perm_ud_table() {
    auto t = std::make_unique<EdgePermUDTable>();
    for (Coord c = 0; c < EDGE_PERM_UD_COORDS; ++c) {
        CubeState s = state_with_edge_perm_ud(decode_edge_perm_ud(c));
        for (int i = 0; i < NUM_G1_MOVES; ++i) {
            CubeState next = s;
            apply_move(next, G1_MOVES[i]);
            (*t)[c][i] = encode_edge_perm_ud(next);
        }
    }
    return t;
}

std::unique_ptr<SlicePermTable> build_slice_perm_table() {
    auto t = std::make_unique<SlicePermTable>();
    for (Coord c = 0; c < SLICE_PERM_COORDS; ++c) {
        CubeState s = state_with_slice_perm(decode_slice_perm(c));
        for (int i = 0; i < NUM_G1_MOVES; ++i) {
            CubeState next = s;
            apply_move(next, G1_MOVES[i]);
            (*t)[c][i] = encode_slice_perm(next);
        }
    }
    return t;
}

}  // namespace

const CornerOriTable& corner_ori_move_table() {
    static const auto table = build_corner_ori_table();
    return *table;
}

const EdgeOriTable& edge_ori_move_table() {
    static const auto table = build_edge_ori_table();
    return *table;
}

const SliceTable& slice_move_table() {
    static const auto table = build_slice_table();
    return *table;
}

const CornerPermTable& corner_perm_move_table() {
    static const auto table = build_corner_perm_table();
    return *table;
}

const EdgePermUDTable& edge_perm_ud_move_table() {
    static const auto table = build_edge_perm_ud_table();
    return *table;
}

const SlicePermTable& slice_perm_move_table() {
    static const auto table = build_slice_perm_table();
    return *table;
}

}
