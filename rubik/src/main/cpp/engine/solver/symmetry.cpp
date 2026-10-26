#include "solver/symmetry.h"

#include <array>
#include <memory>

namespace cube::solver {

namespace {

struct Generator {
    std::array<uint8_t, NUM_CORNERS> corner_perm;
    std::array<uint8_t, NUM_EDGES>   edge_perm;
    bool corner_ori_negate;
};

// S_U2: 180 around vertical axis
constexpr Generator GEN_U2 = {
    {2,3,0,1,6,7,4,5},
    {2,3,0,1,6,7,4,5,10,11,8,9},
    false,
};

// S_F2: 180 around the F-B axis
constexpr Generator GEN_F2 = {
    {5,4,7,6,1,0,3,2},
    {6,5,4,7,2,1,0,3,9,8,11,10},
    false,
};

// S_LR2: reflection through the L/R midplane (x -> -x)
constexpr Generator GEN_LR2 = {
    {1,0,3,2,5,4,7,6},
    {2,1,0,3,6,5,4,7,9,8,11,10},
    true,
};

CubeState apply_generator(const Generator& g, const CubeState& in) {
    // symmetry action on a state = conjugation by the symmetry: both slots and
    // piece-ids are relabeled by the generator's permutation
    //
    // for involutive g (all three of ours are order 2), g_perm == g_perm_inv, so:
    //   new.corner_positions[i] = g_perm[in.corner_positions[g_perm[i]]]
    CubeState out{};
    for (uint8_t i = 0; i < NUM_CORNERS; ++i) {
        uint8_t src = g.corner_perm[i];
        uint8_t piece = in.corner_positions[src];
        out.corner_positions[i] = g.corner_perm[piece];
        uint8_t o = in.corner_orientations[src];
        out.corner_orientations[i] = g.corner_ori_negate ? uint8_t((3 - o) % 3) : o;
    }
    for (uint8_t i = 0; i < NUM_EDGES; ++i) {
        uint8_t src = g.edge_perm[i];
        uint8_t piece = in.edge_positions[src];
        out.edge_positions[i] = g.edge_perm[piece];
        out.edge_orientations[i] = in.edge_orientations[src];
    }
    return out;
}

// enumerate all 8 elements as sequences of generator applications
// all three generators commute (order-2 and pairwise commuting), so composition
// order within apply_element doesn't matter.
CubeState apply_element(int s, const CubeState& in) {
    CubeState c = in;
    if (s & 1) c = apply_generator(GEN_U2, c);
    if (s & 2) c = apply_generator(GEN_F2, c);
    if (s & 4) c = apply_generator(GEN_LR2, c);
    return c;
}

constexpr std::array<uint8_t, NUM_MOVES> CONJ_U2 = {
    0,1,2, 3,4,5, 9,10,11, 6,7,8, 15,16,17, 12,13,14
};
constexpr std::array<uint8_t, NUM_MOVES> CONJ_F2 = {
    3,4,5, 0,1,2, 9,10,11, 6,7,8, 12,13,14, 15,16,17
};
constexpr std::array<uint8_t, NUM_MOVES> CONJ_LR2 = {
    1,0,2, 4,3,5, 10,9,11, 7,6,8, 13,12,14, 16,15,17
};

// compose a permutation-of-moves via conjugation
std::array<uint8_t, NUM_MOVES> compose_conj(
    const std::array<uint8_t, NUM_MOVES>& outer,
    const std::array<uint8_t, NUM_MOVES>& inner)
{
    std::array<uint8_t, NUM_MOVES> out{};
    for (int m = 0; m < NUM_MOVES; ++m) out[m] = outer[inner[m]];
    return out;
}

std::array<std::array<uint8_t, NUM_MOVES>, NUM_SYM> build_conj_move_table() {
    constexpr std::array<uint8_t, NUM_MOVES> IDENTITY = {
        0,1,2, 3,4,5, 6,7,8, 9,10,11, 12,13,14, 15,16,17
    };
    std::array<std::array<uint8_t, NUM_MOVES>, NUM_SYM> t{};
    for (int s = 0; s < NUM_SYM; ++s) {
        auto acc = IDENTITY;
        // apply generators in the same order that apply_element applies them to
        // the state; because all three generators commute pairwise, composition
        // order doesn't affect the result
        if (s & 1) acc = compose_conj(CONJ_U2, acc);
        if (s & 2) acc = compose_conj(CONJ_F2, acc);
        if (s & 4) acc = compose_conj(CONJ_LR2, acc);
        t[s] = acc;
    }
    return t;
}

const std::array<std::array<uint8_t, NUM_MOVES>, NUM_SYM>& CONJ_MOVE_TABLE = *[]() {
    static auto t = build_conj_move_table();
    return &t;
}();

std::unique_ptr<SymCornerPermTable> build_sym_corner_perm() {
    auto t = std::make_unique<SymCornerPermTable>();
    for (int s = 0; s < NUM_SYM; ++s) {
        for (Coord cp = 0; cp < CORNER_PERM_COORDS; ++cp) {
            CubeState c = solved_cube();
            auto perm = decode_corner_perm(cp);
            for (size_t i = 0; i < NUM_CORNERS; ++i) c.corner_positions[i] = perm[i];
            CubeState transformed = apply_element(s, c);
            (*t)[s][cp] = encode_corner_perm(transformed);
        }
    }
    return t;
}

std::unique_ptr<SymEdgePermUDTable> build_sym_edge_perm_ud() {
    auto t = std::make_unique<SymEdgePermUDTable>();
    for (int s = 0; s < NUM_SYM; ++s) {
        for (Coord ep = 0; ep < EDGE_PERM_UD_COORDS; ++ep) {
            CubeState c = solved_cube();
            auto perm = decode_edge_perm_ud(ep);
            for (size_t i = 0; i < 8; ++i) c.edge_positions[i] = perm[i];
            CubeState transformed = apply_element(s, c);
            (*t)[s][ep] = encode_edge_perm_ud(transformed);
        }
    }
    return t;
}

std::unique_ptr<SymSlicePermTable> build_sym_slice_perm() {
    auto t = std::make_unique<SymSlicePermTable>();
    for (int s = 0; s < NUM_SYM; ++s) {
        for (Coord sp = 0; sp < SLICE_PERM_COORDS; ++sp) {
            CubeState c = solved_cube();
            auto perm = decode_slice_perm(sp);
            for (size_t i = 0; i < 4; ++i) c.edge_positions[8 + i] = static_cast<uint8_t>(perm[i] + 8);
            CubeState transformed = apply_element(s, c);
            (*t)[s][sp] = encode_slice_perm(transformed);
        }
    }
    return t;
}

}  // namespace

const std::array<std::array<uint8_t, NUM_MOVES>, NUM_SYM>& sym_conj_move() {
    return CONJ_MOVE_TABLE;
}

CubeState apply_symmetry(int s, const CubeState& c) {
    return apply_element(s, c);
}

const SymCornerPermTable& sym_corner_perm() {
    static const auto t = build_sym_corner_perm();
    return *t;
}
const SymEdgePermUDTable& sym_edge_perm_ud() {
    static const auto t = build_sym_edge_perm_ud();
    return *t;
}
const SymSlicePermTable& sym_slice_perm() {
    static const auto t = build_sym_slice_perm();
    return *t;
}

}
