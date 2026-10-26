#include "solver/coords.h"

#include <cstddef>

namespace cube::solver {

namespace {

// C(n,k) for n in [0-12], k in [0-4]. constexpr so slice lookups are a single load in the IDA* hot path
constexpr std::array<std::array<uint16_t, 5>, 13> make_binom() {
    std::array<std::array<uint16_t, 5>, 13> t{};
    for (int n = 0; n <= 12; ++n) {
        t[n][0] = 1;
        for (int k = 1; k <= 4; ++k) {
            t[n][k] = (n == 0) ? 0 : (t[n - 1][k - 1] + t[n - 1][k]);
        }
    }
    return t;
}
constexpr auto BINOM = make_binom();

constexpr uint16_t C(int n, int k) {
    if (n < k || n < 0 || k < 0) return 0;
    return BINOM[n][k];
}

// equator edges are FR/FL/BL/BR (ids 8-11)
constexpr bool is_equator_edge(uint8_t edge_id) {
    return edge_id >= 8;
}

// 0!-12! for the Lehmer/factorial-base encoding (8! = 40320 is max)
constexpr std::array<uint32_t, 13> make_factorials() {
    std::array<uint32_t, 13> f{};
    f[0] = 1;
    for (int i = 1; i <= 12; ++i) f[i] = f[i - 1] * static_cast<uint32_t>(i);
    return f;
}
constexpr auto FACT = make_factorials();

// Lehmer + factorial-base for permutations of {0-N-1}, identity is 0
// encode: L[i] = count of j>i with perm[j]<perm[i]; coord = sum L[i]*(N-1-i)!
// decode: peel factorial digits then pick the L[i]-th still-unused value
template <size_t N>
uint32_t lehmer_encode(const std::array<uint8_t, N>& perm) {
    uint32_t coord = 0;
    for (size_t i = 0; i < N; ++i) {
        uint8_t lehmer = 0;
        for (size_t j = i + 1; j < N; ++j) {
            if (perm[j] < perm[i]) ++lehmer;
        }
        coord += lehmer * FACT[N - 1 - i];
    }
    return coord;
}

template <size_t N>
std::array<uint8_t, N> lehmer_decode(uint32_t coord) {
    std::array<uint8_t, N> lehmer{};
    for (size_t i = 0; i < N; ++i) {
        uint32_t w = FACT[N - 1 - i];
        lehmer[i] = static_cast<uint8_t>(coord / w);
        coord %= w;
    }
    std::array<uint8_t, N> perm{};
    std::array<bool, N> used{};
    for (size_t i = 0; i < N; ++i) {
        uint8_t skip = lehmer[i];
        for (uint8_t v = 0; v < N; ++v) {
            if (used[v]) continue;
            if (skip == 0) {
                perm[i] = v;
                used[v] = true;
                break;
            }
            --skip;
        }
    }
    return perm;
}

}  // namespace

Coord encode_corner_ori(const CubeState& c) {
    Coord coord = 0;
    for (uint8_t i = 0; i < NUM_CORNERS - 1; ++i) {
        coord = coord * 3 + c.corner_orientations[i];
    }
    return coord;
}

std::array<uint8_t, NUM_CORNERS> decode_corner_ori(Coord coord) {
    std::array<uint8_t, NUM_CORNERS> ori{};
    uint8_t sum = 0;
    for (int i = NUM_CORNERS - 2; i >= 0; --i) {
        ori[i] = coord % 3;
        coord /= 3;
        sum += ori[i];
    }
    ori[NUM_CORNERS - 1] = (3 - sum % 3) % 3;
    return ori;
}

Coord encode_edge_ori(const CubeState& c) {
    Coord coord = 0;
    for (uint8_t i = 0; i < NUM_EDGES - 1; ++i) {
        coord = (coord << 1) | c.edge_orientations[i];
    }
    return coord;
}

std::array<uint8_t, NUM_EDGES> decode_edge_ori(Coord coord) {
    std::array<uint8_t, NUM_EDGES> ori{};
    uint8_t parity = 0;
    for (int i = NUM_EDGES - 2; i >= 0; --i) {
        ori[i] = coord & 1;
        coord >>= 1;
        parity ^= ori[i];
    }
    ori[NUM_EDGES - 1] = parity;
    return ori;
}

// combinadic (colex) rank: coord = C(c3,4)+C(c2,3)+C(c1,2)+C(c0,1), c3>...>c0
Coord encode_slice(const CubeState& c) {
    std::array<uint8_t, 4> slots{};
    uint8_t found = 0;
    for (uint8_t slot = 0; slot < NUM_EDGES; ++slot) {
        if (is_equator_edge(c.edge_positions[slot])) {
            slots[found++] = slot;
        }
    }
    // slots come out ascending, so slots[3] is c3
    return static_cast<Coord>(
        C(slots[3], 4) + C(slots[2], 3) + C(slots[1], 2) + C(slots[0], 1));
}

std::array<uint8_t, 4> decode_slice(Coord coord) {
    // greedy inverse: for k=4..1, pick the largest c with C(c,k) <= remaining
    std::array<uint8_t, 4> slots{};
    int remaining = coord;
    for (int k = 4; k >= 1; --k) {
        int c = k - 1;
        while (c + 1 <= 11 && C(c + 1, k) <= remaining) ++c;
        slots[k - 1] = static_cast<uint8_t>(c);
        remaining -= C(c, k);
    }
    // slots filled descending; return ascending
    return { slots[0], slots[1], slots[2], slots[3] };
}

Phase1Coords phase1_coords_of(const CubeState& c) {
    return {
        encode_corner_ori(c),
        encode_edge_ori(c),
        encode_slice(c),
    };
}

Coord encode_corner_perm(const CubeState& c) {
    return static_cast<Coord>(lehmer_encode<8>(c.corner_positions));
}

std::array<uint8_t, NUM_CORNERS> decode_corner_perm(Coord coord) {
    return lehmer_decode<8>(coord);
}

Coord encode_edge_perm_ud(const CubeState& c) {
    std::array<uint8_t, 8> perm{};
    for (size_t i = 0; i < 8; ++i) perm[i] = c.edge_positions[i];
    return static_cast<Coord>(lehmer_encode<8>(perm));
}

std::array<uint8_t, 8> decode_edge_perm_ud(Coord coord) {
    return lehmer_decode<8>(coord);
}

// normalize equator edge ids {8-11} to {0-3} so identity is coord 0
Coord encode_slice_perm(const CubeState& c) {
    std::array<uint8_t, 4> perm{};
    for (size_t i = 0; i < 4; ++i) {
        perm[i] = static_cast<uint8_t>(c.edge_positions[8 + i] - 8);
    }
    return static_cast<Coord>(lehmer_encode<4>(perm));
}

std::array<uint8_t, 4> decode_slice_perm(Coord coord) {
    return lehmer_decode<4>(coord);
}

Phase2Coords phase2_coords_of(const CubeState& c) {
    return {
        encode_corner_perm(c),
        encode_edge_perm_ud(c),
        encode_slice_perm(c),
    };
}

}
