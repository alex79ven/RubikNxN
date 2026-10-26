#include "cube_nxn/reduction_general.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <map>
#include <queue>
#include <unordered_set>

namespace cube_nxn {

namespace {

CenterFlavor classify_center(int dr, int dc) {
    const int adr = std::abs(dr);
    const int adc = std::abs(dc);
    if (dr == 0 && dc == 0) return CenterFlavor::Fixed;
    if (dr == 0 || dc == 0) return CenterFlavor::PlusCenter;
    if (adr == adc)         return CenterFlavor::XCenter;
    return CenterFlavor::Oblique;
}

int center_ring(int dr, int dc) {
    const int adr = std::abs(dr);
    const int adc = std::abs(dc);
    return (adr > adc) ? adr : adc;
}

}

std::vector<CenterSlot> enumerate_center_slots(int n) {
    assert(n >= 3);
    std::vector<CenterSlot> out;
    for (int f = 0; f < NUM_FACES; ++f) {
        for (int r = 1; r <= n - 2; ++r) {
            for (int c = 1; c <= n - 2; ++c) {
                const int dr = 2 * r - (n - 1);
                const int dc = 2 * c - (n - 1);
                out.push_back(CenterSlot{
                    static_cast<Face>(f), r, c,
                    classify_center(dr, dc), center_ring(dr, dc),
                });
            }
        }
    }
    return out;
}

std::vector<EdgeSlotG> enumerate_edge_slots(int n) {
    assert(n >= 3);
    std::vector<EdgeSlotG> out;
    const int wing_pairs = (n - 2) / 2;
    const bool has_middle = (n % 2 == 1);
    for (int edge_id = 0; edge_id < 12; ++edge_id) {
        for (int w = 0; w < wing_pairs; ++w) {
            for (int side = 0; side < 2; ++side) {
                out.push_back(EdgeSlotG{edge_id, EdgeFlavor::Wing, w, side});
            }
        }
        if (has_middle) {
            out.push_back(EdgeSlotG{edge_id, EdgeFlavor::Middle, -1, 0});
        }
    }
    return out;
}

// move construction helpers
namespace {

inline Move mk_outer(Face f, Turn t) { return Move{f, 0, 0, t}; }

inline Move mk_slice(Face f, int d, Turn t) {
    assert(d >= 1);
    return Move{f, d - 1, d - 1, t};
}

inline Move mk_wide(Face f, int d, Turn t) {
    assert(d >= 2);
    return Move{f, 0, d - 1, t};
}

inline Turn inverse_turn(Turn t) {
    if (t == Turn::CW)  return Turn::CCW;
    if (t == Turn::CCW) return Turn::CW;
    return Turn::Half;
}

inline Move inverse_move(const Move& m) {
    return Move{m.face, m.outer_depth, m.inner_depth, inverse_turn(m.turn)};
}

MoveStep inverse_step(const MoveStep& step) {
    MoveStep out;
    out.reserve(step.size());
    for (auto it = step.rbegin(); it != step.rend(); ++it) out.push_back(inverse_move(*it));
    return out;
}

// commutator templates
MoveStep xcenter_3cycle(int d) {
    return {
        mk_slice(Face::R, d, Turn::CCW),
        mk_outer(Face::D, Turn::CCW),
        mk_slice(Face::B, d, Turn::CW),
        mk_outer(Face::D, Turn::CW),
        mk_slice(Face::R, d, Turn::CW),
        mk_outer(Face::D, Turn::CCW),
        mk_slice(Face::B, d, Turn::CCW),
        mk_outer(Face::D, Turn::CW),
    };
}

MoveStep wing_3cycle(int d) {
    assert(d >= 2);
    auto wide_or_outer = [](int width, Turn t) -> Move {
        if (width == 1) return mk_outer(Face::R, t);
        return mk_wide(Face::R, width, t);
    };
    return {
        wide_or_outer(d - 1, Turn::CW),
        mk_wide(Face::R, d, Turn::CCW),
        mk_outer(Face::U, Turn::CW),
        mk_outer(Face::R, Turn::CW),
        mk_outer(Face::U, Turn::CCW),
        mk_wide(Face::R, d, Turn::CW),
        wide_or_outer(d - 1, Turn::CCW),
        mk_outer(Face::U, Turn::CW),
        mk_outer(Face::R, Turn::CCW),
        mk_outer(Face::U, Turn::CCW),
    };
}

MoveStep plus_center_3cycle(int d, int n) {
    assert(n % 2 == 1);
    const int e_depth = (n + 1) / 2;
    return {
        mk_outer(Face::U, Turn::CW),
        mk_slice(Face::R, d, Turn::CW),
        mk_slice(Face::D, e_depth, Turn::Half),
        mk_slice(Face::R, d, Turn::CCW),
        mk_outer(Face::U, Turn::CCW),
        mk_slice(Face::R, d, Turn::CW),
        mk_slice(Face::D, e_depth, Turn::Half),
        mk_slice(Face::R, d, Turn::CCW),
    };
}

// oblique 3-cycle commutator derivation, self-verifying and returns the first one that works for that exact input
static MoveStep find_clean_oblique_alg(int n, int a, int b, int chir) {
    auto is_target = [&](int dr, int dc) {
        if (classify_center(dr, dc) != CenterFlavor::Oblique) return false;
        int adr = std::abs(dr), adc = std::abs(dc);
        if (std::min(adr, adc) != a || std::max(adr, adc) != b) return false;
        return (dr * dc > 0 ? 1 : -1) == chir;
    };
    auto is_clean = [&](const MoveStep& alg) {
        NxNCube c(n);
        apply_move_step(c, alg);
        int target_moved = 0, other_moved = 0;
        for (int f = 0; f < NUM_FACES; ++f) {
            for (int r = 0; r < n; ++r) {
                for (int col = 0; col < n; ++col) {
                    if (c.sticker(f, r, col) == static_cast<uint8_t>(f)) continue;
                    const int dr = 2 * r - (n - 1), dc = 2 * col - (n - 1);
                    if (is_target(dr, dc)) ++target_moved;
                    else ++other_moved;
                }
            }
        }
        return other_moved == 0 && target_moved == 3;
    };

    const Face faces[6] = {Face::U, Face::R, Face::F, Face::D, Face::L, Face::B};
    const Turn qturns[2] = {Turn::CW, Turn::CCW};
    for (Face fa : faces) {
        for (int da = 1; da <= n; ++da) {
            for (Turn ta : qturns) {
                const Move A = mk_slice(fa, da, ta);
                const Move Ainv = mk_slice(fa, da, ta == Turn::CW ? Turn::CCW : Turn::CW);
                for (Face fb : faces) {
                    if (fb == fa) continue;
                    for (Turn tb : qturns) {
                        const Turn tbInv = (tb == Turn::CW) ? Turn::CCW : Turn::CW;
                        for (Face fc : faces) {
                            for (int dc_ = 1; dc_ <= n; ++dc_) {
                                for (Turn tc : qturns) {
                                    const Turn tcInv = (tc == Turn::CW) ? Turn::CCW : Turn::CW;
                                    MoveStep candidate = {
                                        A,
                                        mk_outer(fb, tb), mk_slice(fc, dc_, tc), mk_outer(fb, tbInv),
                                        Ainv,
                                        mk_outer(fb, tb), mk_slice(fc, dc_, tcInv), mk_outer(fb, tbInv),
                                    };
                                    if (is_clean(candidate)) return candidate;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return {};  // no clean candidate found in this family
}

MoveStep middle_edge_3cycle(int n) {
    assert(n % 2 == 1 && n >= 5);
    const int m_depth = (n + 1) / 2;
    return {
        mk_slice(Face::L, m_depth, Turn::Half),
        mk_outer(Face::U, Turn::CW),
        mk_slice(Face::L, m_depth, Turn::CW),
        mk_outer(Face::U, Turn::Half),
        mk_slice(Face::L, m_depth, Turn::CCW),
        mk_outer(Face::U, Turn::CW),
        mk_slice(Face::L, m_depth, Turn::Half),
    };
}

// flip exactly two middle-edges in place
MoveStep middle_edge_flip(int n) {
    assert(n % 2 == 1 && n >= 5);
    const int m = (n + 1) / 2;
    return {
        mk_slice(Face::R, m, Turn::CCW), mk_outer(Face::U, Turn::CW),
        mk_slice(Face::R, m, Turn::CCW), mk_outer(Face::U, Turn::CW),
        mk_slice(Face::R, m, Turn::CCW), mk_outer(Face::U, Turn::Half),
        mk_slice(Face::R, m, Turn::CW),  mk_outer(Face::U, Turn::CW),
        mk_slice(Face::R, m, Turn::CW),  mk_outer(Face::U, Turn::CW),
        mk_slice(Face::R, m, Turn::CW),  mk_outer(Face::U, Turn::Half),
    };
}

MoveStep oll_parity_general(int k) {
    const int d = k + 1;   // inner-slice depth
    return {
        mk_slice(Face::R, d, Turn::Half),
        mk_outer(Face::B, Turn::Half),
        mk_outer(Face::U, Turn::Half),
        mk_slice(Face::L, d, Turn::CW),
        mk_outer(Face::U, Turn::Half),
        mk_slice(Face::R, d, Turn::CCW),
        mk_outer(Face::U, Turn::Half),
        mk_slice(Face::R, d, Turn::CW),
        mk_outer(Face::U, Turn::Half),
        mk_outer(Face::F, Turn::Half),
        mk_slice(Face::R, d, Turn::CW),
        mk_outer(Face::F, Turn::Half),
        mk_slice(Face::L, d, Turn::CCW),
        mk_outer(Face::B, Turn::Half),
        mk_slice(Face::R, d, Turn::Half),
    };
}

// enumerate all setup moves for a given N: outer faces + slice moves at each valid depth
// Slice depths: 2 through N-1 inclusive
std::vector<Move> setup_moves_for_n(int n) {
    std::vector<Move> moves;
    for (int f = 0; f < 6; ++f) {
        for (Turn t : {Turn::CW, Turn::Half, Turn::CCW}) {
            moves.push_back(mk_outer(static_cast<Face>(f), t));
        }
    }
    for (int f = 0; f < 6; ++f) {
        for (int d = 2; d < n; ++d) {
            for (Turn t : {Turn::CW, Turn::Half, Turn::CCW}) {
                moves.push_back(mk_slice(static_cast<Face>(f), d, t));
            }
        }
    }
    return moves;
}

// a position within an orbit packed into a single uint16
using Pos = uint16_t;
// encode (face, r, c) into a Pos
inline Pos encode_pos(int face, int r, int c) {
    return static_cast<Pos>((face << 12) | ((r & 0x3F) << 6) | (c & 0x3F));
}

inline void decode_pos(Pos p, int& face, int& r, int& c) {
    face = (p >> 12) & 0xF;
    r = (p >> 6) & 0x3F;
    c = p & 0x3F;
}

// maps each position in the orbit to an index
struct OrbitGraph {
    std::vector<Pos>   positions;
    std::vector<int>   pos_to_index;
    std::vector<Move>  moves;
    std::vector<std::vector<int>> perm;
    int  n_cube = 0;
};

// permutation + the concrete cube-move word realizing it
struct PermWord {
    std::vector<int> perm;
    MoveStep word;
};

std::vector<int> identity_perm(int n) {
    std::vector<int> p(n);
    for (int i = 0; i < n; ++i) p[i] = i;
    return p;
}

bool is_identity_perm(const std::vector<int>& p) {
    for (int i = 0; i < static_cast<int>(p.size()); ++i)
        if (p[i] != i) return false;
    return true;
}

std::vector<int> compose_perm(const std::vector<int>& a,
                              const std::vector<int>& b) {
    assert(a.size() == b.size());
    std::vector<int> out(a.size());
    for (int i = 0; i < static_cast<int>(a.size()); ++i) out[i] = b[a[i]];
    return out;
}

PermWord inverse_pw(const PermWord& a) {
    PermWord out;
    out.perm.resize(a.perm.size());
    for (int i = 0; i < static_cast<int>(a.perm.size()); ++i) out.perm[a.perm[i]] = i;
    out.word = inverse_step(a.word);
    return out;
}

// build the OrbitGraph using supplied move set
OrbitGraph build_orbit_graph(int n, const std::function<bool(int,int,int)>& in_orbit,
                             const std::vector<Move>& move_set) {
    OrbitGraph g;
    g.n_cube = n;
    g.pos_to_index.assign(1 << 16, -1);
    g.moves = move_set;
    for (int f = 0; f < NUM_FACES; ++f) {
        for (int r = 0; r < n; ++r) {
            for (int c = 0; c < n; ++c) {
                if (in_orbit(f, r, c)) {
                    Pos p = encode_pos(f, r, c);
                    g.pos_to_index[p] = static_cast<int>(g.positions.size());
                    g.positions.push_back(p);
                }
            }
        }
    }
    const int M = static_cast<int>(move_set.size());
    g.perm.assign(g.positions.size(), std::vector<int>(M, -1));
    assert(g.positions.size() < 128);
    for (int m_idx = 0; m_idx < M; ++m_idx) {
        NxNCube marker(n);
        for (int i = 0; i < static_cast<int>(g.positions.size()); ++i) {
            int f, r, c; decode_pos(g.positions[i], f, r, c);
            marker.set_sticker(f, r, c, static_cast<uint8_t>(128 + i));
        }
        apply_move(marker, move_set[m_idx]);
        for (int dst_idx = 0; dst_idx < static_cast<int>(g.positions.size()); ++dst_idx) {
            int f, r, c; decode_pos(g.positions[dst_idx], f, r, c);
            uint8_t v = marker.sticker(f, r, c);
            if (v >= 128 && v < 128 + (int)g.positions.size()) {
                int src = v - 128;
                g.perm[src][m_idx] = dst_idx;
            }
        }
    }
    return g;
}


// discover the 3 action slots of commutator on a solved cube
struct ActionTriple { int source_idx; int target_idx; int buffer_idx; };

ActionTriple detect_action_triple(const OrbitGraph& g, const MoveStep& alg) {
    const int n = g.n_cube;
    NxNCube marker(n);
    assert(g.positions.size() < 128);
    for (int i = 0; i < static_cast<int>(g.positions.size()); ++i) {
        int f, r, c; decode_pos(g.positions[i], f, r, c);
        marker.set_sticker(f, r, c, static_cast<uint8_t>(128 + i));
    }
    for (const auto& m : alg) apply_move(marker, m);
    std::vector<int> perm_after(g.positions.size(), -1);
    for (int dst = 0; dst < static_cast<int>(g.positions.size()); ++dst) {
        int f, r, c; decode_pos(g.positions[dst], f, r, c);
        uint8_t v = marker.sticker(f, r, c);
        if (v >= 128 && v < 128 + (int)g.positions.size()) perm_after[dst] = v - 128;
    }
    ActionTriple t{-1, -1, -1};
    for (int i = 0; i < static_cast<int>(g.positions.size()); ++i) {
        if (perm_after[i] == -1 || perm_after[i] == i) continue;
        int a = i;
        int b = -1, c = -1;
        for (int j = 0; j < static_cast<int>(g.positions.size()); ++j) {
            if (perm_after[j] == a) { b = j; break; }
        }
        if (b < 0 || b == a) continue;
        for (int j = 0; j < static_cast<int>(g.positions.size()); ++j) {
            if (perm_after[j] == b) { c = j; break; }
        }
        if (c < 0 || c == a || c == b) continue;
        if (perm_after[a] != c) continue;
        t = {a, b, c};
        break;
    }
    return t;
}

// home face for a position in an orbit (the face on which its color matches)
inline int home_face(const OrbitGraph& g, int pos_idx) {
    int f, r, c; decode_pos(g.positions[pos_idx], f, r, c);
    (void)r; (void)c;
    return f;
}

// read the color currently at orbit position pos_idx
inline uint8_t read_color(const NxNCube& cube, const OrbitGraph& g, int pos_idx) {
    int f, r, c; decode_pos(g.positions[pos_idx], f, r, c);
    return cube.sticker(f, r, c);
}

bool permutation_is_even(const std::vector<int>& p) {
    int inversions = 0;
    for (int i = 0; i < static_cast<int>(p.size()); ++i)
        for (int j = i + 1; j < static_cast<int>(p.size()); ++j)
            if (p[i] > p[j]) inversions ^= 1;
    return inversions == 0;
}

std::array<int,3> triple_from_3cycle(const std::vector<int>& p) {
    for (int a = 0; a < static_cast<int>(p.size()); ++a) {
        if (p[a] == a) continue;
        const int b = p[a], c = p[b];
        if (a != b && b != c && c != a && p[c] == a)
            return {a, b, c};
    }
    return {-1, -1, -1};
}

std::vector<int> make_3cycle_perm(int K, int a, int b, int c) {
    std::vector<int> p = identity_perm(K);
    p[a] = b; p[b] = c; p[c] = a;
    return p;
}

// decompose an arbitrary even permutation into oriented 3-cycles
bool decompose_even_perm(const std::vector<int>& target,
                         std::vector<std::array<int,3>>& cycles_out) {
    if (!permutation_is_even(target)) return false;
    const int K = static_cast<int>(target.size());
    std::vector<int> rem = target;
    std::vector<std::vector<int>> eliminators;

    for (;;) {
        int a = -1;
        for (int i = 0; i < K; ++i) {
            if (rem[i] != i && rem[rem[i]] != i) { a = i; break; }
        }
        if (a < 0) break;
        const int b = rem[a], c = rem[b];
        auto op = make_3cycle_perm(K, b, a, c);
        rem = compose_perm(rem, op);
        eliminators.push_back(std::move(op));
    }

    std::vector<std::pair<int,int>> swaps;
    std::vector<uint8_t> used(K, 0);
    for (int i = 0; i < K; ++i) {
        if (rem[i] == i || used[i]) continue;
        const int j = rem[i];
        if (j < 0 || j >= K || rem[j] != i) return false;
        used[i] = used[j] = 1;
        swaps.emplace_back(i, j);
    }
    if (swaps.size() % 2 != 0) return false;

    for (size_t s = 0; s < swaps.size(); s += 2) {
        const int v[4] = {swaps[s].first, swaps[s].second,
                          swaps[s+1].first, swaps[s+1].second};
        std::vector<int> pair_perm = identity_perm(K);
        pair_perm[v[0]] = v[1]; pair_perm[v[1]] = v[0];
        pair_perm[v[2]] = v[3]; pair_perm[v[3]] = v[2];
        std::vector<std::vector<int>> candidates;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) if (j != i)
                for (int k = 0; k < 4; ++k) if (k != i && k != j)
                    candidates.push_back(make_3cycle_perm(K, v[i], v[j], v[k]));
        bool found = false;
        for (const auto& a : candidates) {
            for (const auto& b : candidates) {
                if (is_identity_perm(compose_perm(compose_perm(pair_perm, a), b))) {
                    rem = compose_perm(compose_perm(rem, a), b);
                    eliminators.push_back(a);
                    eliminators.push_back(b);
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found) return false;
    }
    if (!is_identity_perm(rem)) return false;

    cycles_out.clear();
    for (auto it = eliminators.rbegin(); it != eliminators.rend(); ++it) {
        auto inv = inverse_pw(PermWord{*it, {}}).perm;
        auto triple = triple_from_3cycle(inv);
        if (triple[0] < 0) return false;
        cycles_out.push_back(triple);
    }

    std::vector<int> check = identity_perm(K);
    for (const auto& t : cycles_out)
        check = compose_perm(check, make_3cycle_perm(K, t[0], t[1], t[2]));
    return check == target;
}

// Direct conjugation: multi-source BFS over ordered triples. Each seed is a
// clean 3-cycle word plus the ordered slot triple it cycles; the BFS picks the
// seed with the shortest setup for every requested cycle.
using DirectSeed = std::pair<std::array<int,3>, MoveStep>;

struct FaceRotation { int map[NUM_FACES]; };

std::vector<FaceRotation> cube_rotations() {
    // face normals: U,R,F,D,L,B
    const int normals[6][3] = {
        {0, 1, 0}, {1, 0, 0}, {0, 0, 1}, {0,-1, 0}, {-1,0, 0}, {0, 0,-1},
    };
    std::vector<FaceRotation> out;
    int perm[3] = {0,1,2};
    do {
        for (int signs = 0; signs < 8; ++signs) {
            int m[3][3] = {{0}};
            for (int i = 0; i < 3; ++i) m[i][perm[i]] = (signs >> i & 1) ? -1 : 1;
            const int det =
                m[0][0]*(m[1][1]*m[2][2] - m[1][2]*m[2][1]) -
                m[0][1]*(m[1][0]*m[2][2] - m[1][2]*m[2][0]) +
                m[0][2]*(m[1][0]*m[2][1] - m[1][1]*m[2][0]);
            if (det != 1) continue;
            FaceRotation r{};
            for (int f = 0; f < NUM_FACES; ++f) {
                int v[3] = {
                    m[0][0]*normals[f][0] + m[0][1]*normals[f][1] + m[0][2]*normals[f][2],
                    m[1][0]*normals[f][0] + m[1][1]*normals[f][1] + m[1][2]*normals[f][2],
                    m[2][0]*normals[f][0] + m[2][1]*normals[f][1] + m[2][2]*normals[f][2],
                };
                for (int g = 0; g < NUM_FACES; ++g)
                    if (normals[g][0] == v[0] && normals[g][1] == v[1] && normals[g][2] == v[2])
                        r.map[f] = g;
            }
            out.push_back(r);
        }
    } while (std::next_permutation(perm, perm + 3));
    return out;
}

MoveStep rotate_alg(const MoveStep& alg, const FaceRotation& rot) {
    MoveStep out;
    out.reserve(alg.size());
    for (const auto& m : alg)
        out.push_back(Move{static_cast<Face>(rot.map[static_cast<int>(m.face)]),
                           m.outer_depth, m.inner_depth, m.turn});
    return out;
}

std::vector<DirectSeed> build_direct_seeds(
        const MoveStep& alg,
        const std::function<std::array<int,3>(const MoveStep&)>& detect) {
    std::vector<DirectSeed> seeds;
    std::map<std::array<int,3>, bool> seen;
    for (const auto& rot : cube_rotations()) {
        MoveStep a = rotate_alg(alg, rot);
        const auto t = detect(a);
        if (t[0] < 0) continue;
        const MoveStep ia = inverse_step(a);
        auto add = [&](std::array<int,3> q, const MoveStep& w) {
            if (!seen.emplace(q, true).second) return;
            seeds.push_back(DirectSeed{q, w});
        };
        add({t[0], t[1], t[2]}, a);
        add({t[1], t[2], t[0]}, a);
        add({t[2], t[0], t[1]}, a);
        add({t[0], t[2], t[1]}, ia);
        add({t[2], t[1], t[0]}, ia);
        add({t[1], t[0], t[2]}, ia);
    }
    return seeds;
}

bool realize_3cycles_direct(int K, const std::vector<Move>& move_set,
                            const std::function<int(int,int)>& perm,
                            const std::vector<DirectSeed>& seeds,
                            const std::vector<std::array<int,3>>& requested,
                            MoveStep& solution_out) {
    if (requested.empty()) { solution_out.clear(); return true; }
    if (seeds.empty()) return false;
    const int M = static_cast<int>(move_set.size());
    const int64_t count = static_cast<int64_t>(K) * K * K;
    auto enc = [K](int a, int b, int c) {
        return (static_cast<int64_t>(a) * K + b) * K + c;
    };
    auto dec = [K](int64_t s, int& a, int& b, int& c) {
        c = static_cast<int>(s % K);
        b = static_cast<int>((s / K) % K);
        a = static_cast<int>(s / (static_cast<int64_t>(K) * K));
    };
    std::vector<int64_t> parent(count, -1);
    std::vector<int16_t> parent_move(count, -1);
    std::vector<int16_t> source(count, -1);
    std::vector<uint8_t> visited(count, 0);
    std::queue<int64_t> q;
    for (int i = 0; i < static_cast<int>(seeds.size()); ++i) {
        const auto& b = seeds[i].first;
        const int64_t s = enc(b[0], b[1], b[2]);
        if (visited[s]) continue;
        visited[s] = 1;
        source[s] = static_cast<int16_t>(i);
        q.push(s);
    }
    while (!q.empty()) {
        const int64_t cur = q.front(); q.pop();
        int a, b, c; dec(cur, a, b, c);
        for (int m = 0; m < M; ++m) {
            const int na = perm(a, m), nb = perm(b, m), nc = perm(c, m);
            if (na < 0 || nb < 0 || nc < 0) continue;
            const int64_t ns = enc(na, nb, nc);
            if (visited[ns]) continue;
            visited[ns] = 1;
            parent[ns] = cur;
            parent_move[ns] = static_cast<int16_t>(m);
            source[ns] = source[cur];
            q.push(ns);
        }
    }
    solution_out.clear();
    for (const auto& t : requested) {
        const int64_t goal = enc(t[0], t[1], t[2]);
        if (!visited[goal]) return false;
        MoveStep setup;
        int64_t s = goal;
        const int64_t root = enc(seeds[source[goal]].first[0],
                                 seeds[source[goal]].first[1],
                                 seeds[source[goal]].first[2]);
        while (s != root) {
            if (s < 0 || parent[s] < 0) return false;
            setup.push_back(move_set[parent_move[s]]);
            s = parent[s];
        }
        std::reverse(setup.begin(), setup.end());
        const MoveStep& word = seeds[source[goal]].second;
        MoveStep inv = inverse_step(setup);
        solution_out.insert(solution_out.end(), inv.begin(), inv.end());
        solution_out.insert(solution_out.end(), word.begin(), word.end());
        solution_out.insert(solution_out.end(), setup.begin(), setup.end());
    }
    return true;
}

// complete center-orbit solver
// note that same-colored centers are interchangeable, so if
// arbitrary assignment is odd, we will swap the homes of two equal-colored
// stickers to resolve parity if needed, without affecting the solve
bool solve_center_orbit_exact(NxNCube& cube, std::vector<MoveStep>& out_seq,
                              const OrbitGraph& g, const MoveStep& alg) {
    const int K = static_cast<int>(g.positions.size());
    if (K == 0) return true;
    const ActionTriple slots = detect_action_triple(g, alg);
    if (slots.source_idx < 0) return false;

    std::array<std::vector<int>, NUM_FACES> current_by_color;
    std::array<std::vector<int>, NUM_FACES> homes_by_color;
    for (int i = 0; i < K; ++i) {
        const int color = static_cast<int>(read_color(cube, g, i));
        if (color < 0 || color >= NUM_FACES) return false;
        current_by_color[color].push_back(i);
        homes_by_color[home_face(g, i)].push_back(i);
    }

    std::vector<int> target = identity_perm(K);  // current position -> assigned home
    for (int color = 0; color < NUM_FACES; ++color) {
        if (current_by_color[color].size() != homes_by_color[color].size()) return false;
        for (int j = 0; j < static_cast<int>(current_by_color[color].size()); ++j)
            target[current_by_color[color][j]] = homes_by_color[color][j];
    }
    if (!permutation_is_even(target)) {
        bool fixed = false;
        for (int color = 0; color < NUM_FACES && !fixed; ++color) {
            if (current_by_color[color].size() < 2) continue;
            std::swap(target[current_by_color[color][0]], target[current_by_color[color][1]]);
            fixed = true;
        }
        if (!fixed || !permutation_is_even(target)) return false;
    }

    std::vector<std::array<int,3>> cycles;
    if (!decompose_even_perm(target, cycles)) return false;
    MoveStep solution;
    const auto perm_of = [&g](int idx, int m) { return g.perm[idx][m]; };
    const auto detect = [&g](const MoveStep& a) {
        const ActionTriple t = detect_action_triple(g, a);
        return std::array<int,3>{t.source_idx, t.target_idx, t.buffer_idx};
    };
    const auto seeds = build_direct_seeds(alg, detect);
    if (!realize_3cycles_direct(K, g.moves, perm_of, seeds, cycles, solution)) return false;

    NxNCube trial = cube;
    apply_move_step(trial, solution);
    for (int i = 0; i < K; ++i)
        if (read_color(trial, g, i) != static_cast<uint8_t>(home_face(g, i))) return false;
    cube = std::move(trial);
    if (!solution.empty()) out_seq.push_back(std::move(solution));
    return true;
}

std::vector<int> conjugate_component_vertices(const OrbitGraph& g,
                                               const ActionTriple& slots) {
    const int K = static_cast<int>(g.positions.size());
    auto encode = [K](int a, int b, int c) {
        return static_cast<int64_t>(a) * K * K + static_cast<int64_t>(b) * K + c;
    };
    auto decode = [K](int64_t s, int& a, int& b, int& c) {
        c = static_cast<int>(s % K);
        b = static_cast<int>((s / K) % K);
        a = static_cast<int>(s / (K * K));
    };
    const int64_t count = static_cast<int64_t>(K) * K * K;
    const int64_t start = encode(slots.source_idx, slots.target_idx, slots.buffer_idx);
    std::vector<uint8_t> visited(count, 0), vertex_seen(K, 0);
    std::queue<int64_t> q;
    visited[start] = 1; q.push(start);
    while (!q.empty()) {
        const int64_t cur = q.front(); q.pop();
        int a, b, c; decode(cur, a, b, c);
        vertex_seen[a] = vertex_seen[b] = vertex_seen[c] = 1;
        for (int m = 0; m < static_cast<int>(g.moves.size()); ++m) {
            const int na = g.perm[a][m], nb = g.perm[b][m], nc = g.perm[c][m];
            if (na < 0 || nb < 0 || nc < 0) continue;
            const int64_t ns = encode(na, nb, nc);
            if (!visited[ns]) { visited[ns] = 1; q.push(ns); }
        }
    }
    std::vector<int> out;
    for (int i = 0; i < K; ++i) if (vertex_seen[i]) out.push_back(i);
    return out;
}

OrbitGraph induced_orbit_graph(const OrbitGraph& full, const std::vector<int>& vertices) {
    OrbitGraph sub;
    sub.n_cube = full.n_cube;
    sub.moves = full.moves;
    sub.pos_to_index.assign(1 << 16, -1);
    std::vector<int> old_to_new(full.positions.size(), -1);
    for (int old : vertices) {
        const int ni = static_cast<int>(sub.positions.size());
        old_to_new[old] = ni;
        sub.positions.push_back(full.positions[old]);
        sub.pos_to_index[full.positions[old]] = ni;
    }
    sub.perm.assign(vertices.size(), std::vector<int>(sub.moves.size(), -1));
    for (int ni = 0; ni < static_cast<int>(vertices.size()); ++ni) {
        const int old = vertices[ni];
        for (int m = 0; m < static_cast<int>(sub.moves.size()); ++m) {
            const int old_dst = full.perm[old][m];
            if (old_dst >= 0) sub.perm[ni][m] = old_to_new[old_dst];
        }
    }
    return sub;
}


}

std::vector<MoveStep> solve_centers_general(NxNCube& cube) {
    const int n = cube.n();
    assert(n >= 3);
    std::vector<MoveStep> out;
    if (n == 3) return out;

    // max slice depth for X-center rings
    const int max_regular_d = (n % 2 == 1) ? (n - 1) / 2 : n / 2;

    // x-centers, ring-by-ring (smallest ring first for smaller orbit size)
    for (int d = max_regular_d; d >= 2; --d) {
        const int ring = n + 1 - 2 * d;
        auto in_orbit = [ring, n](int, int r, int c) {
            const int dr = 2 * r - (n - 1);
            const int dc = 2 * c - (n - 1);
            if (classify_center(dr, dc) != CenterFlavor::XCenter) return false;
            return center_ring(dr, dc) == ring;
        };
        OrbitGraph g = build_orbit_graph(n, in_orbit, setup_moves_for_n(n));
        if (!solve_center_orbit_exact(cube, out, g, xcenter_3cycle(d))) return out;
    }
    // plus-centers (odd N)
    if (n % 2 == 1) {
        for (int d = max_regular_d; d >= 2; --d) {
            const int ring = n + 1 - 2 * d;
            auto in_orbit = [ring, n](int, int r, int c) {
                const int dr = 2 * r - (n - 1);
                const int dc = 2 * c - (n - 1);
                if (classify_center(dr, dc) != CenterFlavor::PlusCenter) return false;
                return center_ring(dr, dc) == ring;
            };
            OrbitGraph g = build_orbit_graph(n, in_orbit, setup_moves_for_n(n));
            if (!solve_center_orbit_exact(cube, out, g, plus_center_3cycle(d, n))) return out;
        }
    }

    // true obliques may split into independent components even when their
    // coordinate taxonomy puts them in one ring; discover those components
    // from the conjugate action and solve each one independently
    if (n >= 6) {
        std::vector<int> oblique_rings;
        for (int r = 1; r <= n - 2; ++r) {
            for (int c = 1; c <= n - 2; ++c) {
                const int dr = 2 * r - (n - 1);
                const int dc = 2 * c - (n - 1);
                if (classify_center(dr, dc) != CenterFlavor::Oblique) continue;
                int ring_key = std::abs(dr) * 100 + std::abs(dc);
                if (std::abs(dr) > std::abs(dc)) ring_key = std::abs(dc) * 100 + std::abs(dr);
                if (std::find(oblique_rings.begin(), oblique_rings.end(), ring_key) == oblique_rings.end()) {
                    oblique_rings.push_back(ring_key);
                }
            }
        }
        for (int ring_key : oblique_rings) {
            int a = ring_key / 100;
            int b = ring_key % 100;
            auto in_orbit = [n, a, b](int, int r, int c) {
                const int dr = 2 * r - (n - 1);
                const int dc = 2 * c - (n - 1);
                if (classify_center(dr, dc) != CenterFlavor::Oblique) return false;
                int adr = std::abs(dr), adc = std::abs(dc);
                return std::min(adr, adc) == a && std::max(adr, adc) == b;
            };
            OrbitGraph full = build_orbit_graph(n, in_orbit, setup_moves_for_n(n));
            
            std::vector<uint8_t> covered(full.positions.size(), 0);
            std::unordered_set<std::string> component_keys;
            for (int chir : {1, -1}) {
                MoveStep alg = find_clean_oblique_alg(n, a, b, chir);
                if (alg.empty()) continue;
                ActionTriple action = detect_action_triple(full, alg);
                if (action.source_idx < 0) continue;
                std::vector<int> vertices = conjugate_component_vertices(full, action);
                std::sort(vertices.begin(), vertices.end());
                std::string key;
                for (int v : vertices) key.push_back(static_cast<char>(v));
                if (!component_keys.insert(key).second) continue;
                OrbitGraph component = induced_orbit_graph(full, vertices);
                if (!solve_center_orbit_exact(cube, out, component, alg)) return out;
                for (int v : vertices) covered[v] = 1;
            }
            if (std::find(covered.begin(), covered.end(), 0) != covered.end()) return out;
        }
    }

    return out;
}

bool centers_reduced_general(const NxNCube& cube) {
    const int n = cube.n();
    for (int f = 0; f < NUM_FACES; ++f)
        for (int r = 1; r < n - 1; ++r)
            for (int c = 1; c < n - 1; ++c)
                if (cube.sticker(f, r, c) != static_cast<uint8_t>(f)) return false;
    return true;
}


// edge geometry for general N

struct EdgeSticker { Face face; int row; int col; };
struct EdgeSlotGeom {
    Face face_a, face_b;
    std::function<EdgeSticker(int t, int n)> stickers_a;
    std::function<EdgeSticker(int t, int n)> stickers_b;
};

std::vector<EdgeSlotGeom> build_edge_geom() {
    std::vector<EdgeSlotGeom> out(12);
    out[0] = {Face::U, Face::F,
              [](int t, int n){ return EdgeSticker{Face::U, n-1, t}; },
              [](int t, int){ return EdgeSticker{Face::F, 0,   t}; }};
    out[1] = {Face::U, Face::R,
              [](int t, int n){ return EdgeSticker{Face::U, n-1-t, n-1}; },
              [](int t, int){ return EdgeSticker{Face::R, 0, t}; }};
    out[2] = {Face::U, Face::B,
              [](int t, int n){ return EdgeSticker{Face::U, 0, n-1-t}; },
              [](int t, int){ return EdgeSticker{Face::B, 0, t}; }};
    out[3] = {Face::U, Face::L,
              [](int t, int){ return EdgeSticker{Face::U, t, 0}; },
              [](int t, int){ return EdgeSticker{Face::L, 0, t}; }};
    out[4] = {Face::D, Face::F,
              [](int t, int){ return EdgeSticker{Face::D, 0, t}; },
              [](int t, int n){ return EdgeSticker{Face::F, n-1, t}; }};
    out[5] = {Face::D, Face::R,
              [](int t, int n){ return EdgeSticker{Face::D, t, n-1}; },
              [](int t, int n){ return EdgeSticker{Face::R, n-1, t}; }};
    out[6] = {Face::D, Face::B,
              [](int t, int n){ return EdgeSticker{Face::D, n-1, n-1-t}; },
              [](int t, int n){ return EdgeSticker{Face::B, n-1, t}; }};
    out[7] = {Face::D, Face::L,
              [](int t, int n){ return EdgeSticker{Face::D, n-1-t, 0}; },
              [](int t, int n){ return EdgeSticker{Face::L, n-1, t}; }};
    out[8] = {Face::F, Face::R,
              [](int t, int n){ return EdgeSticker{Face::F, t, n-1}; },
              [](int t, int){ return EdgeSticker{Face::R, t, 0}; }};
    out[9] = {Face::R, Face::B,
              [](int t, int n){ return EdgeSticker{Face::R, t, n-1}; },
              [](int t, int){ return EdgeSticker{Face::B, t, 0}; }};
    out[10] = {Face::B, Face::L,
              [](int t, int n){ return EdgeSticker{Face::B, t, n-1}; },
              [](int t, int){ return EdgeSticker{Face::L, t, 0}; }};
    out[11] = {Face::L, Face::F,
              [](int t, int n){ return EdgeSticker{Face::L, t, n-1}; },
              [](int t, int){ return EdgeSticker{Face::F, t, 0}; }};
    return out;
}
inline std::pair<int,int> wing_ts(int w, int n) {
    return {w + 1, n - 2 - w};
}


// piece-tracking wing solver

struct PieceOrbit {
    int n;
    int t1, t2;
    std::vector<EdgeSticker> face_a_stickers;
    std::vector<EdgeSticker> face_b_stickers;
    std::vector<int>          home_face_a;
    std::vector<int>          home_face_b;
    std::vector<Move>         moves;
    std::vector<std::vector<int>> perm;
    // perm_flip[i][m] = true iff applying move m to a piece at slot i lands
    // its face_b sticker where face_a's tracked position is; needed for middle edges only
    std::vector<std::vector<bool>> perm_flip;
};

// build the piece orbit for wing_index w on N-cube (using canonical edge order)
PieceOrbit build_wing_piece_orbit(int n, int w, const std::vector<EdgeSlotGeom>& edges,
                                  const std::vector<Move>& move_set) {
    PieceOrbit p;
    p.n = n;
    p.t1 = w + 1;
    p.t2 = n - 2 - w;
    for (int e = 0; e < 12; ++e) {
        for (int side = 0; side < 2; ++side) {
            int t = (side == 0) ? p.t1 : p.t2;
            EdgeSticker sa = edges[e].stickers_a(t, n);
            EdgeSticker sb = edges[e].stickers_b(t, n);
            p.face_a_stickers.push_back(sa);
            p.face_b_stickers.push_back(sb);
            p.home_face_a.push_back(static_cast<int>(edges[e].face_a));
            p.home_face_b.push_back(static_cast<int>(edges[e].face_b));
        }
    }
    const int K = static_cast<int>(p.face_a_stickers.size());
    p.moves = move_set;
    const int M = static_cast<int>(move_set.size());
    p.perm.assign(K, std::vector<int>(M, -1));
    p.perm_flip.assign(K, std::vector<bool>(M, false));
    constexpr int kFaceAOffset = 32;
    constexpr int kFaceBOffset = 160;
    for (int m_idx = 0; m_idx < M; ++m_idx) {
        NxNCube marker(n);
        for (int i = 0; i < K; ++i) {
            const EdgeSticker& sa = p.face_a_stickers[i];
            const EdgeSticker& sb = p.face_b_stickers[i];
            marker.set_sticker(static_cast<int>(sa.face), sa.row, sa.col, static_cast<uint8_t>(kFaceAOffset + i));
            marker.set_sticker(static_cast<int>(sb.face), sb.row, sb.col, static_cast<uint8_t>(kFaceBOffset + i));
        }
        apply_move(marker, move_set[m_idx]);
        for (int dst_idx = 0; dst_idx < K; ++dst_idx) {
            const EdgeSticker& sa = p.face_a_stickers[dst_idx];
            uint8_t v = marker.sticker(static_cast<int>(sa.face), sa.row, sa.col);
            int src;
            bool flipped = false;
            if (v >= kFaceAOffset && v < kFaceAOffset + K)      src = v - kFaceAOffset;
            else if (v >= kFaceBOffset && v < kFaceBOffset + K) { src = v - kFaceBOffset; flipped = true; }
            else continue;
            p.perm_flip[src][m_idx] = flipped;
            p.perm[src][m_idx] = dst_idx;
        }
    }
    if (std::getenv("CUBE_DEBUG_EXACT_WINGS")) {
        int missing = 0;
        for (int i = 0; i < K; ++i) for (int m = 0; m < M; ++m)
            if (p.perm[i][m] < 0) {
                ++missing;
                if (missing <= 20)
                    std::fprintf(stderr, "  missing slot=%d edge=%d side=%d move=%s\n",
                                 i, i/2, i%2, format_move(p.moves[m]).c_str());
            }
        std::fprintf(stderr, "[exact-wings] orbit w=%d missing transitions=%d/%d\n",
                     w, missing, K * M);
    }
    return p;
}

// build the piece orbit for the 12 middle-edge pieces (odd N only, one sticker-pair per edge at t=(n-1)/2)
PieceOrbit build_middle_piece_orbit(int n, const std::vector<EdgeSlotGeom>& edges,
                                    const std::vector<Move>& move_set) {
    PieceOrbit p;
    p.n = n;
    const int t_mid = (n - 1) / 2;
    p.t1 = t_mid;
    p.t2 = t_mid;
    for (int e = 0; e < 12; ++e) {
        EdgeSticker sa = edges[e].stickers_a(t_mid, n);
        EdgeSticker sb = edges[e].stickers_b(t_mid, n);
        p.face_a_stickers.push_back(sa);
        p.face_b_stickers.push_back(sb);
        p.home_face_a.push_back(static_cast<int>(edges[e].face_a));
        p.home_face_b.push_back(static_cast<int>(edges[e].face_b));
    }
    const int K = static_cast<int>(p.face_a_stickers.size());
    p.moves = move_set;
    const int M = static_cast<int>(move_set.size());
    p.perm.assign(K, std::vector<int>(M, -1));
    p.perm_flip.assign(K, std::vector<bool>(M, false));
    constexpr int kFaceAOffset = 32;
    constexpr int kFaceBOffset = 160;
    for (int m_idx = 0; m_idx < M; ++m_idx) {
        NxNCube marker(n);
        for (int i = 0; i < K; ++i) {
            const EdgeSticker& sa = p.face_a_stickers[i];
            const EdgeSticker& sb = p.face_b_stickers[i];
            marker.set_sticker(static_cast<int>(sa.face), sa.row, sa.col, static_cast<uint8_t>(kFaceAOffset + i));
            marker.set_sticker(static_cast<int>(sb.face), sb.row, sb.col, static_cast<uint8_t>(kFaceBOffset + i));
        }
        apply_move(marker, move_set[m_idx]);
        for (int dst_idx = 0; dst_idx < K; ++dst_idx) {
            const EdgeSticker& sa = p.face_a_stickers[dst_idx];
            uint8_t v = marker.sticker(static_cast<int>(sa.face), sa.row, sa.col);
            int src;
            bool flipped;
            if (v >= kFaceAOffset && v < kFaceAOffset + K)      { src = v - kFaceAOffset; flipped = false; }
            else if (v >= kFaceBOffset && v < kFaceBOffset + K) { src = v - kFaceBOffset; flipped = true; }
            else continue;
            p.perm_flip[src][m_idx] = flipped;
            p.perm[src][m_idx] = dst_idx;
        }
    }
    return p;
}

inline std::pair<uint8_t, uint8_t> read_pair(const NxNCube& cube, const PieceOrbit& p, int i) {
    const EdgeSticker& sa = p.face_a_stickers[i];
    const EdgeSticker& sb = p.face_b_stickers[i];
    uint8_t ca = cube.sticker(static_cast<int>(sa.face), sa.row, sa.col);
    uint8_t cb = cube.sticker(static_cast<int>(sb.face), sb.row, sb.col);
    if (ca > cb) std::swap(ca, cb);
    return {ca, cb};
}

inline std::pair<uint8_t, uint8_t> home_pair(const PieceOrbit& p, int i) {
    uint8_t a = static_cast<uint8_t>(p.home_face_a[i]);
    uint8_t b = static_cast<uint8_t>(p.home_face_b[i]);
    if (a > b) std::swap(a, b);
    return {a, b};
}

inline bool piece_solved(const NxNCube& cube, const PieceOrbit& p, int i) {
    return read_pair(cube, p, i) == home_pair(p, i);
}

// orientation-sensitive check
inline bool piece_solved_exact(const NxNCube& cube, const PieceOrbit& p, int i) {
    const EdgeSticker& sa = p.face_a_stickers[i];
    const EdgeSticker& sb = p.face_b_stickers[i];
    uint8_t ca = cube.sticker(static_cast<int>(sa.face), sa.row, sa.col);
    uint8_t cb = cube.sticker(static_cast<int>(sb.face), sb.row, sb.col);
    return ca == static_cast<uint8_t>(p.home_face_a[i]) && cb == static_cast<uint8_t>(p.home_face_b[i]);
}


// detect the 3-cycle action of the alg on the piece orbit
struct WingActionTriple { int source_idx; int target_idx; int buffer_idx; };
WingActionTriple detect_wing_action(const PieceOrbit& p, const MoveStep& alg) {
    const int n = p.n;
    const int K = static_cast<int>(p.face_a_stickers.size());
    constexpr int kFaceAOffset = 32;
    constexpr int kFaceBOffset = 160;
    NxNCube marker(n);
    for (int i = 0; i < K; ++i) {
        const EdgeSticker& sa = p.face_a_stickers[i];
        const EdgeSticker& sb = p.face_b_stickers[i];
        marker.set_sticker(static_cast<int>(sa.face), sa.row, sa.col, static_cast<uint8_t>(kFaceAOffset + i));
        marker.set_sticker(static_cast<int>(sb.face), sb.row, sb.col, static_cast<uint8_t>(kFaceBOffset + i));
    }
    for (const auto& m : alg) apply_move(marker, m);
    std::vector<int> perm_after(K, -1);
    for (int dst = 0; dst < K; ++dst) {
        const EdgeSticker& sa = p.face_a_stickers[dst];
        uint8_t v = marker.sticker(static_cast<int>(sa.face), sa.row, sa.col);
        int src;
        if (v >= kFaceAOffset && v < kFaceAOffset + K)      src = v - kFaceAOffset;
        else if (v >= kFaceBOffset && v < kFaceBOffset + K) src = v - kFaceBOffset;
        else continue;
        perm_after[dst] = src;
    }
    WingActionTriple t{-1, -1, -1};
    for (int i = 0; i < K; ++i) {
        if (perm_after[i] == i) continue;
        int a = i, b = -1, c = -1;
        for (int j = 0; j < K; ++j) if (perm_after[j] == a) { b = j; break; }
        if (b < 0 || b == a) continue;
        for (int j = 0; j < K; ++j) if (perm_after[j] == b) { c = j; break; }
        if (c < 0 || c == a || c == b) continue;
        if (perm_after[a] != c) continue;
        t = {a, b, c};
        break;
    }
    return t;
}

bool solve_wing_orbit_exact(NxNCube& cube, std::vector<MoveStep>& out_seq,
                            const PieceOrbit& p, const MoveStep& alg) {
    const int K = static_cast<int>(p.face_a_stickers.size());
    const WingActionTriple slots = detect_wing_action(p, alg);
    if (slots.source_idx < 0) {
        if (std::getenv("CUBE_DEBUG_EXACT_WINGS")) std::fprintf(stderr, "[exact-wings] no action triple\n");
        return false;
    }

    std::map<std::pair<uint8_t,uint8_t>, std::vector<int>> current, homes;
    for (int i = 0; i < K; ++i) {
        current[read_pair(cube, p, i)].push_back(i);
        homes[home_pair(p, i)].push_back(i);
    }
    if (current.size() != homes.size()) {
        if (std::getenv("CUBE_DEBUG_EXACT_WINGS")) {
            std::fprintf(stderr, "[exact-wings] pair key count mismatch current=%zu homes=%zu\n", current.size(), homes.size());
            for (const auto& [pair, pos] : current)
                std::fprintf(stderr, "  current (%d,%d) x%zu\n", pair.first, pair.second, pos.size());
        }
        return false;
    }
    std::vector<int> target = identity_perm(K);
    for (const auto& [pair, positions] : current) {
        auto it = homes.find(pair);
        if (it == homes.end() || it->second.size() != positions.size()) {
            if (std::getenv("CUBE_DEBUG_EXACT_WINGS"))
                std::fprintf(stderr, "[exact-wings] pair multiplicity mismatch (%d,%d) current=%zu home=%zu\n",
                             pair.first, pair.second, positions.size(),
                             it == homes.end() ? std::size_t{0} : it->second.size());
            return false;
        }
        for (int j = 0; j < static_cast<int>(positions.size()); ++j)
            target[positions[j]] = it->second[j];
    }
    if (!permutation_is_even(target)) {
        bool fixed = false;
        for (const auto& [pair, positions] : current) {
            (void)pair;
            if (positions.size() < 2) continue;
            std::swap(target[positions[0]], target[positions[1]]);
            fixed = true;
            break;
        }
        if (!fixed || !permutation_is_even(target)) return false;
    }

    std::vector<std::array<int,3>> cycles;
    if (!decompose_even_perm(target, cycles)) return false;
    MoveStep solution;
    const auto perm_of = [&p](int idx, int m) { return p.perm[idx][m]; };
    const auto detect = [&p](const MoveStep& a) {
        const WingActionTriple t = detect_wing_action(p, a);
        return std::array<int,3>{t.source_idx, t.target_idx, t.buffer_idx};
    };
    const auto seeds = build_direct_seeds(alg, detect);
    if (!realize_3cycles_direct(K, p.moves, perm_of, seeds, cycles, solution)) return false;

    NxNCube trial = cube;
    apply_move_step(trial, solution);
    for (int i = 0; i < K; ++i) if (!piece_solved(trial, p, i)) return false;
    if (std::getenv("CUBE_DEBUG_EXACT_WINGS")) {
        int exact = 0;
        for (int i = 0; i < K; ++i) if (piece_solved_exact(trial, p, i)) ++exact;
        int paired = 0;
        for (int e = 0; e < 12; ++e) {
            const auto& a0 = p.face_a_stickers[2*e];
            const auto& b0 = p.face_b_stickers[2*e];
            const auto& a1 = p.face_a_stickers[2*e+1];
            const auto& b1 = p.face_b_stickers[2*e+1];
            if (trial.sticker((int)a0.face,a0.row,a0.col) == trial.sticker((int)a1.face,a1.row,a1.col) &&
                trial.sticker((int)b0.face,b0.row,b0.col) == trial.sticker((int)b1.face,b1.row,b1.col)) ++paired;
        }
        std::fprintf(stderr, "[exact-wings] K=%d unordered=%d exact=%d paired=%d/12\n", K, K, exact, paired);
    }
    cube = std::move(trial);
    if (!solution.empty()) out_seq.push_back(std::move(solution));
    return true;
}

bool apply_wing_permutation_exact(NxNCube& cube, std::vector<MoveStep>& out_seq,
                                  const PieceOrbit& p, const MoveStep& alg,
                                  const std::vector<int>& target) {
    const int K = static_cast<int>(p.face_a_stickers.size());
    if (static_cast<int>(target.size()) != K || !permutation_is_even(target)) return false;
    const WingActionTriple slots = detect_wing_action(p, alg);
    if (slots.source_idx < 0) return false;
    std::vector<std::array<int,3>> cycles;
    if (!decompose_even_perm(target, cycles)) return false;
    MoveStep solution;
    const auto perm_of = [&p](int idx, int m) { return p.perm[idx][m]; };
    const auto detect = [&p](const MoveStep& a) {
        const WingActionTriple t = detect_wing_action(p, a);
        return std::array<int,3>{t.source_idx, t.target_idx, t.buffer_idx};
    };
    const auto seeds = build_direct_seeds(alg, detect);
    if (!realize_3cycles_direct(K, p.moves, perm_of, seeds, cycles, solution)) return false;
    NxNCube trial = cube;
    apply_move_step(trial, solution);
    cube = std::move(trial);
    if (!solution.empty()) out_seq.push_back(std::move(solution));
    return true;
}

bool find_complete_pair_setup(const PieceOrbit& p, int from_a, int from_b,
                              int to_a, int to_b, MoveStep& setup_out);

std::pair<uint8_t,uint8_t> read_ordered_pair(const NxNCube& cube,
                                             const PieceOrbit& p, int slot) {
    const auto& a = p.face_a_stickers[slot];
    const auto& b = p.face_b_stickers[slot];
    return {cube.sticker((int)a.face, a.row, a.col),
            cube.sticker((int)b.face, b.row, b.col)};
}

bool apply_wing_pair_flip_exact(NxNCube& cube, std::vector<MoveStep>& out,
                                const PieceOrbit& p, const MoveStep& flip_alg,
                                int edge_id) {
    const int a = 2 * edge_id, b = a + 1;
    const auto before_a = read_ordered_pair(cube, p, a);
    const auto before_b = read_ordered_pair(cube, p, b);
    if (before_a != before_b) return false;
    MoveStep setup;
    if (!find_complete_pair_setup(p, a, b, 0, 1, setup)) {
        if (!find_complete_pair_setup(p, a, b, 1, 0, setup)) return false;
    }
    MoveStep operation = setup;
    operation.insert(operation.end(), flip_alg.begin(), flip_alg.end());
    MoveStep undo = inverse_step(setup);
    operation.insert(operation.end(), undo.begin(), undo.end());
    NxNCube trial = cube;
    apply_move_step(trial, operation);
    const auto after_a = read_ordered_pair(trial, p, a);
    const auto after_b = read_ordered_pair(trial, p, b);
    if (after_a != after_b || after_a.first != before_a.second ||
        after_a.second != before_a.first) return false;
    cube = std::move(trial);
    out.push_back(std::move(operation));
    return true;
}

bool find_complete_pair_setup(const PieceOrbit& p, int from_a, int from_b,
                              int to_a, int to_b, MoveStep& setup_out) {
    const int K = static_cast<int>(p.face_a_stickers.size());
    const int start = from_a * K + from_b;
    const int goal = to_a * K + to_b;
    if (start == goal) { setup_out.clear(); return true; }
    std::vector<int> parent(K * K, -1), parent_move(K * K, -1);
    std::vector<uint8_t> visited(K * K, 0);
    std::queue<int> q;
    visited[start] = 1; q.push(start);
    while (!q.empty()) {
        const int cur = q.front(); q.pop();
        const int a = cur / K, b = cur % K;
        for (int m = 0; m < static_cast<int>(p.moves.size()); ++m) {
            const int na = p.perm[a][m], nb = p.perm[b][m];
            if (na < 0 || nb < 0) continue;
            const int ns = na * K + nb;
            if (visited[ns]) continue;
            visited[ns] = 1;
            parent[ns] = cur;
            parent_move[ns] = m;
            if (ns == goal) {
                setup_out.clear();
                for (int s = goal; s != start; s = parent[s])
                    setup_out.push_back(p.moves[parent_move[s]]);
                std::reverse(setup_out.begin(), setup_out.end());
                return true;
            }
            q.push(ns);
        }
    }
    return false;
}

bool solve_middle_edges_exact(NxNCube& cube, std::vector<MoveStep>& out,
                              const PieceOrbit& p, const MoveStep& cycle_alg,
                              const MoveStep& flip_alg) {
    const int K = static_cast<int>(p.face_a_stickers.size());
    const NxNCube before = cube;
    std::vector<MoveStep> generated;
    const WingActionTriple slots = detect_wing_action(p, cycle_alg);
    if (slots.source_idx < 0) return false;

    std::map<std::pair<uint8_t,uint8_t>, int> home_for_pair;
    for (int i = 0; i < K; ++i) home_for_pair[home_pair(p, i)] = i;
    if (static_cast<int>(home_for_pair.size()) != K) return false;
    std::vector<int> target(K, -1);
    for (int i = 0; i < K; ++i) {
        auto it = home_for_pair.find(read_pair(cube, p, i));
        if (it == home_for_pair.end()) return false;
        target[i] = it->second;
    }
    if (!permutation_is_even(target)) return false;

    std::vector<std::array<int,3>> cycles;
    if (!decompose_even_perm(target, cycles)) return false;
    MoveStep position_solution;
    const auto perm_of = [&p](int idx, int m) { return p.perm[idx][m]; };
    const auto detect = [&p](const MoveStep& a) {
        const WingActionTriple t = detect_wing_action(p, a);
        return std::array<int,3>{t.source_idx, t.target_idx, t.buffer_idx};
    };
    const auto seeds = build_direct_seeds(cycle_alg, detect);
    if (!realize_3cycles_direct(K, p.moves, perm_of, seeds, cycles, position_solution)) return false;
    NxNCube trial = cube;
    apply_move_step(trial, position_solution);
    for (int i = 0; i < K; ++i) if (!piece_solved(trial, p, i)) return false;
    if (!position_solution.empty()) generated.push_back(std::move(position_solution));

    std::vector<int> flipped;
    for (int i = 0; i < K; ++i) if (!piece_solved_exact(trial, p, i)) flipped.push_back(i);
    if (flipped.size() % 2 != 0) return false;
    while (!flipped.empty()) {
        const int a = flipped[0], b = flipped[1];
        MoveStep setup;
        if (!find_complete_pair_setup(p, a, b, 0, 2, setup)) {
            if (!find_complete_pair_setup(p, a, b, 2, 0, setup)) return false;
        }
        MoveStep operation = setup;
        operation.insert(operation.end(), flip_alg.begin(), flip_alg.end());
        MoveStep undo = inverse_step(setup);
        operation.insert(operation.end(), undo.begin(), undo.end());
        apply_move_step(trial, operation);
        generated.push_back(std::move(operation));

        flipped.clear();
        for (int i = 0; i < K; ++i) {
            if (!piece_solved(trial, p, i)) return false;
            if (!piece_solved_exact(trial, p, i)) flipped.push_back(i);
        }
    }

    // pure middle-edge stage must leave every other sticker byte-for-byte unchanged
    std::vector<uint8_t> is_middle(before.num_stickers(), 0);
    for (int i = 0; i < K; ++i) {
        const auto& a = p.face_a_stickers[i];
        const auto& b = p.face_b_stickers[i];
        const int fs = p.n * p.n;
        is_middle[(int)a.face * fs + a.row * p.n + a.col] = 1;
        is_middle[(int)b.face * fs + b.row * p.n + b.col] = 1;
    }
    for (int i = 0; i < before.num_stickers(); ++i)
        if (!is_middle[i] && trial.raw()[i] != before.raw()[i]) return false;
    cube = std::move(trial);
    out.insert(out.end(), std::make_move_iterator(generated.begin()),
               std::make_move_iterator(generated.end()));
    return true;
}

void solve_middles_general(NxNCube& cube, std::vector<MoveStep>& out, int n,
                          const std::vector<EdgeSlotGeom>& edges) {
    if (n % 2 == 0) return;
    PieceOrbit p = build_middle_piece_orbit(n, edges, setup_moves_for_n(n));
    bool ok = solve_middle_edges_exact(cube, out, p,
                                       middle_edge_3cycle(n), middle_edge_flip(n));
    if (std::getenv("CUBE_DEBUG_EXACT_MIDDLES")) {
        std::fprintf(stderr, "[exact-middles] ok=%d final_solved=%d\n",
                     ok ? 1 : 0, cube.is_solved() ? 1 : 0);
    }
}

std::vector<MoveStep> solve_edges_general(NxNCube& cube) {
    const int n = cube.n();
    assert(n >= 3);
    if (n == 3) return {};

    std::vector<MoveStep> out;
    const auto edges = build_edge_geom();

    const int wing_pairs = (n - 2) / 2;
    for (int w = 0; w < wing_pairs; ++w) {
        PieceOrbit orbit = build_wing_piece_orbit(n, w, edges, setup_moves_for_n(n));
        if (!solve_wing_orbit_exact(cube, out, orbit, wing_3cycle(w + 2))) return out;
    }


    // align every inner wing orbit to orbit 0 using pure two-wing flip conjugate
    PieceOrbit reference = build_wing_piece_orbit(n, 0, edges, setup_moves_for_n(n));
    for (int w = 1; w < wing_pairs; ++w) {
        PieceOrbit orbit = build_wing_piece_orbit(n, w, edges, setup_moves_for_n(n));
        for (int e = 0; e < 12; ++e) {
            const auto ref = read_ordered_pair(cube, reference, 2 * e);
            const auto cur = read_ordered_pair(cube, orbit, 2 * e);
            if (cur == ref) continue;
            if (cur.first != ref.second || cur.second != ref.first) return out;
            if (!apply_wing_pair_flip_exact(cube, out, orbit,
                                            oll_parity_general(w + 1), e)) return out;
        }
    }

    ParityState parity = detect_parity_general(cube);
    if (parity == ParityState::OLL || parity == ParityState::Both) {
        for (int w = 0; w < wing_pairs; ++w) {
            PieceOrbit orbit = build_wing_piece_orbit(n, w, edges, setup_moves_for_n(n));
            if (!apply_wing_pair_flip_exact(cube, out, orbit,
                                            oll_parity_general(w + 1), 0)) return out;
        }
    }

    parity = detect_parity_general(cube);
    if (parity == ParityState::PLL || parity == ParityState::Both) {
        for (int w = 0; w < wing_pairs; ++w) {
            PieceOrbit orbit = build_wing_piece_orbit(n, w, edges, setup_moves_for_n(n));
            std::vector<int> swap_groups = identity_perm(24);
            std::swap(swap_groups[0], swap_groups[2]);
            std::swap(swap_groups[1], swap_groups[3]);
            if (!apply_wing_permutation_exact(cube, out, orbit,
                                              wing_3cycle(w + 2), swap_groups)) return out;
        }
    }

    return out;
}

bool wings_reduced_general(const NxNCube& cube) {
    const int n = cube.n();
    if (n == 3) return true;
    const auto edges = build_edge_geom();
    const int wing_pairs = (n - 2) / 2;
    for (int e = 0; e < 12; ++e) {
        std::pair<uint8_t,uint8_t> reference{255,255};
        for (int w = 0; w < wing_pairs; ++w) {
            const auto [t1, t2] = wing_ts(w, n);
            const auto a1 = edges[e].stickers_a(t1, n);
            const auto b1 = edges[e].stickers_b(t1, n);
            const auto a2 = edges[e].stickers_a(t2, n);
            const auto b2 = edges[e].stickers_b(t2, n);
            const std::pair<uint8_t,uint8_t> p1{
                cube.sticker((int)a1.face, a1.row, a1.col),
                cube.sticker((int)b1.face, b1.row, b1.col)};
            const std::pair<uint8_t,uint8_t> p2{
                cube.sticker((int)a2.face, a2.row, a2.col),
                cube.sticker((int)b2.face, b2.row, b2.col)};
            if (p1 != p2 || p1.first == p1.second) return false;
            if (w == 0) reference = p1;
            else if (p1 != reference) return false;
        }
    }
    return true;
}

std::vector<MoveStep> solve_middles_after_kociemba(NxNCube& cube) {
    const int n = cube.n();
    if (n % 2 == 0) return {};
    std::vector<MoveStep> out;
    const auto edges = build_edge_geom();
    solve_middles_general(cube, out, n, edges);
    return out;
}

// public accessors

MoveStep xcenter_3cycle_alg(int d)            { return xcenter_3cycle(d); }
MoveStep wing_3cycle_alg(int d)               { return wing_3cycle(d); }
MoveStep plus_center_3cycle_alg(int d, int n) { return plus_center_3cycle(d, n); }
MoveStep oblique_3cycle_alg(int n, int a, int b, int chir) { return find_clean_oblique_alg(n, a, b, chir); }
MoveStep middle_edge_3cycle_alg(int n)        { return middle_edge_3cycle(n); }
MoveStep middle_edge_flip_alg(int n)          { return middle_edge_flip(n); }

}