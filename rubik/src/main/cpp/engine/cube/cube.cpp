#include "cube/cube.h"

#include <array>

namespace cube {

CubeState solved_cube() {
    CubeState c{};
    for (uint8_t i = 0; i < NUM_CORNERS; ++i) {
        c.corner_positions[i] = i;
        c.corner_orientations[i] = 0;
    }
    for (uint8_t i = 0; i < NUM_EDGES; ++i) {
        c.edge_positions[i] = i;
        c.edge_orientations[i] = 0;
    }
    return c;
}

// slot ordering:
//   corners 0-7:  URF, UFL, ULB, UBR, DFR, DLF, DBL, DRB
//   edges   0-11: UR,  UF,  UL,  UB,  DR,  DF,  DL,  DB,  FR, FL, BL, BR
// convention: perm[i]=j means "slot i now holds the piece formerly at slot j"

struct MoveTable {
    std::array<uint8_t, NUM_CORNERS> corner_perm;
    std::array<uint8_t, NUM_CORNERS> corner_orient_delta;
    std::array<uint8_t, NUM_EDGES>   edge_perm;
    std::array<uint8_t, NUM_EDGES>   edge_orient_delta;
};

namespace {

// clockwise quarter-turn tables, cross-verified against pycuber
// twist sign convention: opposite of pycuber (+1/+2 swapped) but consistent
// and sums to 0 mod 3. only F/B flip edges

// U: corners 0->1->2->3->0, edges 0->1->2->3->0
constexpr MoveTable MOVE_U = {
    {3,0,1,2,4,5,6,7},
    {0,0,0,0,0,0,0,0},
    {3,0,1,2,4,5,6,7,8,9,10,11},
    {0,0,0,0,0,0,0,0,0,0,0,0},
};

// D: corners 4->7->6->5->4, edges 4->7->6->5->4
constexpr MoveTable MOVE_D = {
    {0,1,2,3,5,6,7,4},
    {0,0,0,0,0,0,0,0},
    {0,1,2,3,5,6,7,4,8,9,10,11},
    {0,0,0,0,0,0,0,0,0,0,0,0},
};

// R: corners 4->0->3->7->4, edges 8->0->11->4->8
constexpr MoveTable MOVE_R = {
    {4,1,2,0,7,5,6,3},
    {2,0,0,1,1,0,0,2},
    {8,1,2,3,11,5,6,7,4,9,10,0},
    {0,0,0,0,0,0,0,0,0,0,0,0},
};

// L: corners 5->6->2->1->5, edges 9->6->10->2->9
constexpr MoveTable MOVE_L = {
    {0,2,6,3,4,1,5,7},
    {0,1,2,0,0,2,1,0},
    {0,1,10,3,4,5,9,7,8,2,6,11},
    {0,0,0,0,0,0,0,0,0,0,0,0},
};

// F: corners 0->4->5->1->0, edges 1->8->5->9->1 (all four affected edges flip)
constexpr MoveTable MOVE_F = {
    {1,5,2,3,0,4,6,7},
    {1,2,0,0,2,1,0,0},
    {0,9,2,3,4,8,6,7,1,5,10,11},
    {0,1,0,0,0,1,0,0,1,1,0,0},
};

// B: corners 3->2->6->7->3, edges 3->10->7->11->3 (all four affected edges flip)
constexpr MoveTable MOVE_B = {
    {0,1,3,7,4,5,2,6},
    {0,0,1,2,0,0,2,1},
    {0,1,2,11,4,5,6,10,8,9,3,7},
    {0,0,0,1,0,0,0,1,0,0,1,1},
};

constexpr std::array<MoveTable, 6> QUARTER_TURNS = {
    MOVE_U, MOVE_D, MOVE_R, MOVE_L, MOVE_F, MOVE_B,
};

CubeState apply_table(const CubeState& in, const MoveTable& t) {
    CubeState out{};
    for (uint8_t i = 0; i < NUM_CORNERS; ++i) {
        uint8_t src = t.corner_perm[i];
        out.corner_positions[i]    = in.corner_positions[src];
        out.corner_orientations[i] = (in.corner_orientations[src] + t.corner_orient_delta[i]) % 3;
    }
    for (uint8_t i = 0; i < NUM_EDGES; ++i) {
        uint8_t src = t.edge_perm[i];
        out.edge_positions[i]    = in.edge_positions[src];
        out.edge_orientations[i] = (in.edge_orientations[src] + t.edge_orient_delta[i]) % 2;
    }
    return out;
}

// compose: first apply `a`, then `b`
MoveTable compose(const MoveTable& a, const MoveTable& b) {
    MoveTable r{};
    for (uint8_t i = 0; i < NUM_CORNERS; ++i) {
        uint8_t mid = b.corner_perm[i];
        r.corner_perm[i] = a.corner_perm[mid];
        r.corner_orient_delta[i] =
            (a.corner_orient_delta[mid] + b.corner_orient_delta[i]) % 3;
    }
    for (uint8_t i = 0; i < NUM_EDGES; ++i) {
        uint8_t mid = b.edge_perm[i];
        r.edge_perm[i] = a.edge_perm[mid];
        r.edge_orient_delta[i] =
            (a.edge_orient_delta[mid] + b.edge_orient_delta[i]) % 2;
    }
    return r;
}

std::array<MoveTable, NUM_MOVES> build_all_move_tables() {
    std::array<MoveTable, NUM_MOVES> tables{};
    for (uint8_t face = 0; face < 6; ++face) {
        const MoveTable& q = QUARTER_TURNS[face];
        MoveTable q2 = compose(q, q);
        MoveTable q3 = compose(q2, q);
        tables[face * 3 + 0] = q;
        tables[face * 3 + 1] = q3;   // prime = 3 quarter turns
        tables[face * 3 + 2] = q2;   // half turn
    }
    return tables;
}

const std::array<MoveTable, NUM_MOVES>& move_tables() {
    static const std::array<MoveTable, NUM_MOVES> tables = build_all_move_tables();
    return tables;
}

}  // namespace

void apply_move(CubeState& c, Move m) {
    const MoveTable& t = move_tables()[static_cast<uint8_t>(m)];
    c = apply_table(c, t);
}

bool is_solved(const CubeState& c) {
    for (uint8_t i = 0; i < NUM_CORNERS; ++i) {
        if (c.corner_positions[i] != i) return false;
        if (c.corner_orientations[i] != 0) return false;
    }
    for (uint8_t i = 0; i < NUM_EDGES; ++i) {
        if (c.edge_positions[i] != i) return false;
        if (c.edge_orientations[i] != 0) return false;
    }
    return true;
}

}
