#include "cube/scramble.h"

#include <random>

namespace cube {

namespace {

// 0=U 1=D 2=R 3=L 4=F 5=B, matching the Move enum (3 variants per face)
uint8_t face_of(Move m) {
    return static_cast<uint8_t>(m) / 3;
}

}  // namespace

std::vector<Move> random_scramble(int length, uint64_t seed) {
    std::vector<Move> result;
    if (length <= 0) return result;
    result.reserve(length);

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> face_dist(0, 5);
    std::uniform_int_distribution<int> variant_dist(0, 2);

    uint8_t last_face = 0xFF;  // sentinel for "no previous move"
    for (int i = 0; i < length; ++i) {
        int face;
        do {
            face = face_dist(rng);
        } while (static_cast<uint8_t>(face) == last_face);

        int variant = variant_dist(rng);
        result.push_back(static_cast<Move>(face * 3 + variant));
        last_face = static_cast<uint8_t>(face);
    }

    return result;
}

}
